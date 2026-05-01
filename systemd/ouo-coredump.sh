#!/bin/sh
#
# Extract stack trace from the most recent ouo core dump.
# Called by ouo-coredump@.service on server crash.

# Give coredumpd time to process the dump
sleep 2

echo "=== Core dump info ==="
coredumpctl info /opt/ouo/run/ouo --no-pager 2>&1 || true

echo "=== Stack trace ==="
coredumpctl debug /opt/ouo/run/ouo --no-pager \
	-A "-batch -ex 'thread apply all bt full' -ex quit" 2>&1 || true
