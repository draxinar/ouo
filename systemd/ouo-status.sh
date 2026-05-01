#!/bin/sh
# ouo-status.sh - write server status as a static JSON file for the web page
# Runs periodically via ouo-status.timer.
set -eu

: "${OUO_WEBDIR:=/var/lib/ouo/web}"

if systemctl is-active --quiet ouo.service; then
	STATUS=online
else
	STATUS=offline
fi

printf '{"status":"%s","timestamp":%d}\n' "$STATUS" "$(date +%s)" \
	> "$OUO_WEBDIR/status.json.tmp" \
	&& mv "$OUO_WEBDIR/status.json.tmp" "$OUO_WEBDIR/status.json"
