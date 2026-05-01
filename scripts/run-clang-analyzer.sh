#!/bin/bash
# Run clang static analyzer via scan-build with the project's disabled
# checker list (.clang-analyzer-disabled).
#
# Usage: scripts/run-clang-analyzer.sh [extra scan-build args...]

set -euo pipefail

cd "$(dirname "$0")/.."

disable_args=()
while IFS= read -r line; do
	line=${line%%#*}
	line=${line##[[:space:]]}
	line=${line%%[[:space:]]}
	[ -z "$line" ] && continue
	disable_args+=(-disable-checker "$line")
done < .clang-analyzer-disabled

exec scan-build --status-bugs "${disable_args[@]}" "$@" make CC=gcc
