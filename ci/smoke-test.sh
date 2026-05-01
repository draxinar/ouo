#!/bin/bash
set -eu

PORT="${1:-2593}"
BINARY="./ouo"
LOG="/tmp/ouo-smoke.log"
TIMEOUT=30

echo "Starting server on port $PORT..."
$BINARY -p "$PORT" > "$LOG" 2>&1 &
SRV_PID=$!

for i in $(seq 1 $TIMEOUT); do
	if ! kill -0 $SRV_PID 2>/dev/null; then
		echo "::error::Server crashed during startup"
		cat "$LOG"
		exit 1
	fi
	if ss -tlnp | grep -q ":$PORT "; then
		echo "Server is listening on port $PORT (after ${i}s)"
		break
	fi
	sleep 1
done

if ! ss -tlnp | grep -q ":$PORT "; then
	echo "::error::Server did not start within ${TIMEOUT}s"
	cat "$LOG"
	exit 1
fi

kill $SRV_PID
wait $SRV_PID || true
echo "Server shut down cleanly"
