#!/bin/bash
# Run clang static analyzer via scan-build with the project's disabled
# checker list (.clang-analyzer-disabled).
#
# Usage: scripts/run-clang-analyzer.sh [extra scan-build args...]

set -euo pipefail

cd "$(dirname "$0")/.."

# scan-build silences a core checker, and hands any other name to
# clang as -analyzer-disable-checker, which fails every analysis when
# clang lacks that checker. The list names checkers of several clang
# versions, so such a name is skipped, with a note.
knows() {
	clang --analyze -Xclang -analyzer-disable-checker -Xclang "$1" \
		-x c /dev/null -o /dev/null 2>/dev/null
}

disable_args=()
while IFS= read -r line; do
	line=${line%%#*}
	line=${line##[[:space:]]}
	line=${line%%[[:space:]]}
	[ -z "$line" ] && continue
	case $line in
	core*) ;;
	*)
		if ! knows "$line"; then
			echo "run-clang-analyzer.sh: clang $(clang -dumpversion) has no checker $line: skipped" >&2
			continue
		fi
		;;
	esac
	disable_args+=(-disable-checker "$line")
done < .clang-analyzer-disabled

exec scan-build --status-bugs "${disable_args[@]}" "$@" make CC=gcc
