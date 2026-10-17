#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD malformed RTL8822C reference trace consumer contract.
# A long register row must be rejected explicitly, without truncation.
set -eu
[ "$#" -eq 0 ] || exit 2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw8822c-trace-input.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
ruby - "$tools" "$work" <<'RUBY'
tools, work = ARGV
%w[mac phy].each do |kind|
  data = File.readlines(File.join(tools, "rtw8822c-#{kind}.trace"))
  index = data.index { |line| line.start_with?('w') }
  abort 'No actual register row' unless index
  data[index] = data[index].chomp + ('X' * 256) + "\n"
  File.write(File.join(work, "#{kind}.trace"), data.join)
end
RUBY
failures=0
for kind in mac phy; do
    if RTW8822C_TRACE="$work/$kind.trace" \
        sh "$tools/rtw8822c-$kind-contract.sh" > "$work/$kind.out" 2> "$work/$kind.err"; then
        echo "$kind: overlong register row was accepted" >&2
        failures=$((failures + 1))
    elif ! grep -qx 'trace entry too long' "$work/$kind.err"; then
        echo "$kind: missing explicit overlong-row rejection" >&2
        cat "$work/$kind.err" >&2
        failures=$((failures + 1))
    fi
done
[ "$failures" -eq 0 ] || exit 1
echo '2 malformed RTL8822C trace inputs rejected'
