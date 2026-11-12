#!/bin/bash
# Lint and smoke tests. Exit code != 0 on the first failure.
set -e
T=$(cd "$(dirname "$0")" && pwd)
for f in $(find "$T/../files" -name "*.php" -o -name "*.inc"); do php -l "$f" >/dev/null; done
echo "lint ok"
"$T/setup.sh" >/dev/null
for t in "$T"/php/[!_]*.php; do
	echo "== $(basename "$t")"
	out=$(cd "$T/.work" && SCG_W="$T/.work" php "$t" 2>&1)
	echo "$out"
	if echo "$out" | grep -qE "PHP (Fatal|Warning|Notice)|FAIL"; then echo "FAILED: $t"; exit 1; fi
done
echo "all tests passed"
