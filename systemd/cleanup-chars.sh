#!/bin/bash
# cleanup-chars.sh - cull orphan/excess player characters from a remote
# server's live save, preserving everything else.
#
# A single account can accumulate more than the 5 character slots the
# client shows (the character-select list orders live characters before
# archived ones and displays only 5, so extras become unreachable and
# appear to "disappear" when a character logs out and archives). This
# script downloads the live save + access.list, removes player characters
# that are orphaned (account not in access.list), empty-named, or beyond
# the per-account keep limit (default 5, ranked by play time then creation
# time) - along with their owned items, pets, houses, and corpses - and
# leaves all other entities untouched.
#
# It uses ../uotools: dynconv (binary<->text) and dynstrip -c (the cull).
#
# Usage: ./systemd/cleanup-chars.sh [--apply] [-m N] [user@host]
#
#   (default)    Dry run: download, cull, print the report, install nothing.
#   --apply      Stop the service, cull, install (live files backed up as
#                *.bkp.<stamp>), and restart.
#   -m N         Per-account keep limit (default 5).
#
# Default target: djc@serpent-isle.com
# Requires sudo on the remote (no password).
#
# Rollback (if --apply produced a bad save):
#   ssh <target> "sudo systemctl stop ouo && \
#       sudo mv /opt/ouo/.rundir/britannia/dynamic0.mul.bkp.<stamp> \
#               /opt/ouo/.rundir/britannia/dynamic0.mul && \
#       sudo mv /opt/ouo/.rundir/britannia/dynidx0.mul.bkp.<stamp> \
#               /opt/ouo/.rundir/britannia/dynidx0.mul && \
#       sudo systemctl start ouo"
set -euo pipefail

APPLY=0
KEEP_LIMIT=5
while [ $# -gt 0 ]; do
	case $1 in
		--apply) APPLY=1; shift ;;
		-m) KEEP_LIMIT="${2:?-m requires an argument}"; shift 2 ;;
		--*) echo "Usage: $0 [--apply] [-m N] [user@host]" >&2; exit 1 ;;
		*) break ;;
	esac
done

SRCDIR="$(cd "$(dirname "$0")/.." && pwd)"
TOOLSDIR="$(cd "$SRCDIR/../uotools" && pwd)"
TARGET="${1:-djc@serpent-isle.com}"
DESTDIR=/opt/ouo
BD="$DESTDIR/.rundir/britannia"
AL="$DESTDIR/.rundir/access.list"
S="sudo -n"
STAMP=$(date +%Y%m%d-%H%M%S)

DYNCONV="$TOOLSDIR/dynconv/dynconv"
DYNSTRIP="$TOOLSDIR/dynutil/dynstrip"

run() {
	ssh -T "$TARGET" "$@"
}

echo "Building cull tooling..."
make -C "$TOOLSDIR/dynconv" >/dev/null
make -C "$TOOLSDIR/dynutil" >/dev/null

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT
mkdir -p "$TMPDIR/saved" "$TMPDIR/out"

if [ "$APPLY" -eq 1 ]; then
	echo "Stopping ouo on $TARGET (apply mode)..."
	run "$S systemctl stop ouo"
fi

# britannia/ is mode 700 owned by ouo. Stage the files via sudo cp into
# /tmp/ (world-readable) so we can scp them down as the SSH user.
echo "Downloading save + access.list from $TARGET..."
run "$S cp -p $BD/dynamic0.mul /tmp/dynamic0.mul.dl && \
	$S cp -p $BD/dynidx0.mul /tmp/dynidx0.mul.dl && \
	$S cp -p $AL /tmp/access.list.dl && \
	$S chmod 644 /tmp/dynamic0.mul.dl /tmp/dynidx0.mul.dl /tmp/access.list.dl"
scp -q "$TARGET:/tmp/dynamic0.mul.dl" "$TARGET:/tmp/dynidx0.mul.dl" \
	"$TARGET:/tmp/access.list.dl" "$TMPDIR/saved/"
run "$S rm /tmp/dynamic0.mul.dl /tmp/dynidx0.mul.dl /tmp/access.list.dl"
mv "$TMPDIR/saved/dynamic0.mul.dl" "$TMPDIR/saved/dynamic0.mul"
mv "$TMPDIR/saved/dynidx0.mul.dl" "$TMPDIR/saved/dynidx0.mul"
mv "$TMPDIR/saved/access.list.dl" "$TMPDIR/saved/access.list"

echo "Culling (keep limit $KEEP_LIMIT per account)..."
"$DYNCONV" -d "$TMPDIR/saved/dynidx0.mul" "$TMPDIR/saved/dynamic0.mul" \
	"$TMPDIR/decoded.txt"
# dynstrip prints the per-category cull report to stderr.
"$DYNSTRIP" -c "$TMPDIR/saved/access.list" -m "$KEEP_LIMIT" \
	"$TMPDIR/decoded.txt" > "$TMPDIR/culled.txt"
"$DYNCONV" -e "$TMPDIR/culled.txt" "$TMPDIR/out/dynidx0.mul" \
	"$TMPDIR/out/dynamic0.mul"

if [ "$APPLY" -eq 0 ]; then
	echo ""
	echo "Dry run complete. Re-run with --apply to install the culled save."
	exit 0
fi

echo "Uploading culled save to $TARGET:/tmp/..."
scp -q "$TMPDIR/out/dynamic0.mul" "$TMPDIR/out/dynidx0.mul" "$TARGET:/tmp/"

echo "Backing up live files and installing culled ones..."
run "$S mv $BD/dynamic0.mul $BD/dynamic0.mul.bkp.$STAMP && \
	$S mv $BD/dynidx0.mul $BD/dynidx0.mul.bkp.$STAMP && \
	$S install -m 644 -o ouo -g ouo /tmp/dynamic0.mul $BD/dynamic0.mul && \
	$S install -m 644 -o ouo -g ouo /tmp/dynidx0.mul $BD/dynidx0.mul && \
	rm /tmp/dynamic0.mul /tmp/dynidx0.mul"
echo "$BD/dynamic0.mul"
echo "$BD/dynidx0.mul"
echo "$BD/dynamic0.mul.bkp.$STAMP"
echo "$BD/dynidx0.mul.bkp.$STAMP"

echo "Starting ouo on $TARGET..."
run "$S systemctl start ouo"

echo "Culled $TARGET. Server restarted."
