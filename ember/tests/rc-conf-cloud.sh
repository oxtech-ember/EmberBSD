#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted standalone rc.conf cloud selection regression.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
selected=${RC_CONF_CLOUD_CASE:-all}
case "$selected" in all|arm64|armimage|emuimage) ;; *) exit 1;; esac
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-rc-conf-cloud.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/arm64/etc" "$work/empty-path"

# Execute the real image generator's customization function. The inherited
# board customization is outside this fragment's standalone-source contract.
awk '
    /^customize\(\) \{/ { copying = 1; found = 1 }
    copying { print }
    copying && /^}/ { exit }
    END { if (!found) exit 1 }
' "$src/distrib/utils/embedded/conf/arm64.conf" > "$work/customize.sh"
(
    mnt="$work/arm64"
    ec2_init=BUILD_TIME_VALUE
    dhcpcd_flags=BUILD_TIME_FLAGS
    customize_evbarm() { :; }
    . "$work/customize.sh"
    customize
)
for image in armimage emuimage; do
    case "$image" in
    armimage) template="$src/distrib/evbarm/liveimage/armimage/rc.conf.armimage";;
    emuimage) template="$src/distrib/amd64/liveimage/emuimage/rc.conf.emuimage";;
    esac
    # Keep the actual configuration tail. Replace only the hardware detection
    # boundary with a controlled is_ec2 result in the runtime shell below.
    awk '
        /^certctl_init=YES$/ { copying = 1; found = 1 }
        copying { print }
        END { if (!found) exit 1 }
    ' "$template" > "$work/$image.conf"
done

run_case() (
    image=$1 cloud=$2 initial=$3
    PATH="$work/empty-path"
    export PATH
    unset -f checkyesno 2>/dev/null || :
    if command -v checkyesno >/dev/null 2>&1; then
        echo 'fixture unexpectedly provides checkyesno' >&2
        exit 1
    fi
    is_cloud() { printf '%s\n' "$cloud"; }
    is_ec2() { printf '%s\n' "$cloud"; }
    dev_exists() { printf 'NO\n'; }
    dhcpcd_flags=$initial
    case "$image" in
    arm64) fragment="$work/arm64/etc/rc.conf";;
    *) fragment="$work/$image.conf";;
    esac
    . "$fragment"
    test "$ec2_init" = "$cloud"
    expected=$initial
    case "$cloud:$image" in
    YES:*) expected="$initial -w";;
    NO:arm64) expected="$initial -b";;
    esac
    if [ "$dhcpcd_flags" != "$expected" ]; then
        printf 'FAIL %s cloud=%s: flags <%s>, expected <%s>\n' \
            "$image" "$cloud" "$dhcpcd_flags" "$expected" >&2
        exit 1
    fi
)
count=0
for image in arm64 armimage emuimage; do
    test "$selected" = all || test "$selected" = "$image" || continue
    for cloud in YES NO; do
        for initial in '' '-q' '-q --timeout 7'; do
            run_case "$image" "$cloud" "$initial"
            count=$((count + 1))
        done
    done
done
printf 'rc.conf cloud selection: PASS %s cases (%s), no rc.subr\n' "$count" "$selected"
