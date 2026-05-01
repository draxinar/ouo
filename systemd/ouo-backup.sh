#!/bin/sh
# ouo-backup.sh - backup dynamic savegame files
#
# The server saves to dynidx0.mul + dynamic0.mul every ~17 minutes.
# Before each save, it copies the current .mul files to .bkp files,
# then overwrites the .mul files. Neither operation is atomic and
# there is no locking.
#
# Strategy: copy the .bkp files (last known-good state before the
# most recent save). These are only touched briefly at the start of
# a save cycle and are stable the rest of the time. They are at most
# one save cycle (~17 min) behind the .mul files but are always a
# consistent pair, unlike the .mul files which may be mid-write.
#
# Retention tiers (all configurable via /etc/default/ouo):
#   - Hourly:  keep all backups for KEEP_HOURS hours (default 48)
#   - Daily:   thin to one/day for KEEP_DAYS days (default 30)
#   - Weekly:  thin to one/week for KEEP_WEEKS weeks (default 52)
#   - Monthly: keep one/month forever
#
# Runs periodically via ouo-backup.timer.
set -eu

: "${SERVERNAME:=britannia}"
: "${OUO_BACKUP_DIR:=/opt/ouo/backups}"
: "${OUO_BACKUP_KEEP_HOURS:=48}"
: "${OUO_BACKUP_KEEP_DAYS:=30}"
: "${OUO_BACKUP_KEEP_WEEKS:=52}"

DATADIR="/opt/ouo/.rundir/$SERVERNAME"
STAMP=$(date +%Y%m%d-%H%M%S)

# Verify source files exist
if [ ! -f "$DATADIR/dynamic0.bkp" ]; then
	echo "backup: $DATADIR/dynamic0.bkp not found, skipping"
	exit 0
fi

mkdir -p "$OUO_BACKUP_DIR"

# Skip if savegame hasn't changed since the last backup
LATEST=$(ls -1t "$OUO_BACKUP_DIR"/[0-9]*.tar.zst 2>/dev/null | head -1)
if [ -n "$LATEST" ] && [ ! "$DATADIR/dynamic0.bkp" -nt "$LATEST" ]; then
	echo "backup: savegame unchanged, skipping"
	exit 0
fi

# Create compressed backup
# Copy files to a temp dir, then tar+zstd into the backup directory.
# zstd at level 19 gives near-lzma compression at much higher speed.
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

for f in dynidx0.bkp dynamic0.bkp; do
	if [ -f "$DATADIR/$f" ]; then
		cp -p "$DATADIR/$f" "$TMPDIR/$f"
	fi
done

if [ -f "$DATADIR/access.list" ]; then
	cp -p "$DATADIR/access.list" "$TMPDIR/access.list"
fi

tar cf - -C "$TMPDIR" . | zstd -19 -o "$OUO_BACKUP_DIR/$STAMP.tar.zst"
echo "backup: saved $OUO_BACKUP_DIR/$STAMP.tar.zst"

# Retention: four-tier thinning
#
# Walk backups oldest-first. Each backup falls into exactly one tier
# based on its age. Within a tier, we keep only the last backup per
# time bucket (day, week, or month) and remove earlier ones in the
# same bucket.
#
# Tier boundaries (from newest to oldest):
#   < KEEP_HOURS   -> hourly:  keep all
#   < KEEP_DAYS    -> daily:   one per day
#   < KEEP_WEEKS   -> weekly:  one per week (ISO week number)
#   >= KEEP_WEEKS  -> monthly: one per month (kept forever)

HOUR_CUTOFF=$(date -d "-${OUO_BACKUP_KEEP_HOURS} hours" +%Y%m%d%H%M%S 2>/dev/null) || \
	HOUR_CUTOFF=$(date -v-${OUO_BACKUP_KEEP_HOURS}H +%Y%m%d%H%M%S)
DAY_CUTOFF=$(date -d "-${OUO_BACKUP_KEEP_DAYS} days" +%Y%m%d%H%M%S 2>/dev/null) || \
	DAY_CUTOFF=$(date -v-${OUO_BACKUP_KEEP_DAYS}d +%Y%m%d%H%M%S)
WEEK_CUTOFF=$(date -d "-${OUO_BACKUP_KEEP_WEEKS} weeks" +%Y%m%d%H%M%S 2>/dev/null) || \
	WEEK_CUTOFF=$(date -v-${OUO_BACKUP_KEEP_WEEKS}w +%Y%m%d%H%M%S)

PREV_DAY=""
PREV_WEEK=""
PREV_MONTH=""
PREV_DAY_FILE=""
PREV_WEEK_FILE=""
PREV_MONTH_FILE=""

for f in "$OUO_BACKUP_DIR"/[0-9]*.tar.zst; do
	[ -f "$f" ] || continue
	NAME=$(basename "$f" .tar.zst)
	# YYYYMMDD from YYYYMMDD-HHMMSS
	DAY=$(echo "$NAME" | cut -c1-8)
	MONTH=$(echo "$NAME" | cut -c1-6)
	TS=$(echo "$NAME" | tr -d '-')

	# Hourly tier: keep everything
	if [ "$TS" -ge "$HOUR_CUTOFF" ] 2>/dev/null; then
		continue
	fi

	# Daily tier: one per day
	if [ "$TS" -ge "$DAY_CUTOFF" ] 2>/dev/null; then
		if [ "$DAY" = "$PREV_DAY" ]; then
			rm -f "$PREV_DAY_FILE"
			echo "backup: thinned $PREV_DAY_FILE"
		fi
		PREV_DAY="$DAY"
		PREV_DAY_FILE="$f"
		continue
	fi

	# Weekly tier: one per ISO week
	# Derive YYYYWW from the date portion of the filename
	YEAR=$(echo "$NAME" | cut -c1-4)
	MON=$(echo "$NAME" | cut -c5-6)
	MDAY=$(echo "$NAME" | cut -c7-8)
	WEEK=$(date -d "${YEAR}-${MON}-${MDAY}" +%G%V 2>/dev/null) || \
		WEEK=$(date -j -f "%Y-%m-%d" "${YEAR}-${MON}-${MDAY}" +%G%V 2>/dev/null) || \
		WEEK="$DAY"

	if [ "$TS" -ge "$WEEK_CUTOFF" ] 2>/dev/null; then
		if [ "$WEEK" = "$PREV_WEEK" ]; then
			rm -f "$PREV_WEEK_FILE"
			echo "backup: thinned $PREV_WEEK_FILE"
		fi
		PREV_WEEK="$WEEK"
		PREV_WEEK_FILE="$f"
		continue
	fi

	# Monthly tier: one per month, kept forever
	if [ "$MONTH" = "$PREV_MONTH" ]; then
		rm -f "$PREV_MONTH_FILE"
		echo "backup: thinned $PREV_MONTH_FILE"
	fi
	PREV_MONTH="$MONTH"
	PREV_MONTH_FILE="$f"
done
