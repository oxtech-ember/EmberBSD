#!/bin/sh
# Origin: EmberBSD - check confirmation and watchdog stop boundaries.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
guard="$src/ember/boot/a133-firstboot-guard.sh"
[ -f "$guard" ] || { echo 'FAIL: first-boot confirmation guard is missing' >&2; exit 1; }
. "$guard"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-firstboot.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
marker="$tmp/pending confirmation"
waits=0
stops=0
action=keep
expected_delay=120
a133_firstboot_wait()
{
    [ "$1" = "$expected_delay" ] || exit 1
    waits=$((waits + 1))
    case "$action" in
        cancel) rm "$marker" ;;
        link) rm "$marker"; ln -s "$tmp/other" "$marker" ;;
        fail) return 1 ;;
    esac
}
a133_firstboot_recover()
{
    stops=$((stops + 1))
}
a133_firstboot_stop()
{
    echo 'FAIL: an unconfirmed running kernel must request a clean reboot' >&2
    return 1
}
a133_firstboot_guard "$marker" 120
[ "$waits:$stops" = 0:0 ]
: >"$tmp/other"
ln -s "$tmp/other" "$marker"
a133_firstboot_guard "$marker" 120
[ "$waits:$stops" = 0:0 ]
rm "$marker"
: >"$marker"
a133_firstboot_guard "$marker" 120
[ "$waits:$stops" = 1:1 ]
action=cancel
a133_firstboot_guard "$marker" 120
[ "$waits:$stops" = 2:1 ]
: >"$marker"
action=link
a133_firstboot_guard "$marker" 120
[ "$waits:$stops" = 3:1 ]
rm "$marker"
: >"$marker"
action=fail
if a133_firstboot_guard "$marker" 120; then exit 1; fi
[ "$waits:$stops" = 4:1 ]
action=keep
for expected_delay in 1 600; do
    a133_firstboot_guard "$marker" "$expected_delay"
done
[ "$waits:$stops" = 6:3 ]
expected_delay=120
for delay in '' 0 601 -1 abc '1;reboot' 999999999999999999999; do
    if a133_firstboot_guard "$marker" "$delay" 2>/dev/null; then exit 1; fi
done
if a133_firstboot_guard "$marker"; then exit 1; fi
if a133_firstboot_guard "$marker" 120 extra; then exit 1; fi
[ "$waits:$stops" = 6:3 ]
a133_firstboot_recover()
{
    return 1
}
if a133_firstboot_guard "$marker" 120; then exit 1; fi
echo 'First-boot guard: pending, confirmed, symlink, wait failure and argument checks passed'
