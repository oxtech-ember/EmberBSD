#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD test-runner error propagation checks, AI-assisted.
# A negative parser test must not convert an analyzer failure to success.
set -eu
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-runner-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/failed-analyzer" <<'RUNNER'
#!/bin/sh
printf 'trace entry too long\n' >&2
exit 97
RUNNER
chmod 755 "$work/failed-analyzer"
if TEST_RUNNER="$work/failed-analyzer" \
    sh "$tools/rtw8822c-trace-input-contract.sh" > "$work/out" 2> "$work/err"; then
    echo 'Analyzer exit 97 was hidden by the parser rejection test' >&2
    exit 1
else
    status=$?
fi
[ "$status" -eq 1 ]
[ "$(grep -c 'expected parser rejection (1), received 97' "$work/err")" -eq 2 ]
echo '2 negative parser paths preserve analyzer failures'
