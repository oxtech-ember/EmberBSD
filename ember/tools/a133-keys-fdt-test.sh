#!/bin/sh
# Origin: EmberBSD - run the A133 keys contract with the selected SDK host compiler.
set -eu
: "${CC:?select the SDK host compiler}"
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${1:-$(CDPATH= cd -- "$tools/../.." && pwd)}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/a133-keys.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
sunxi="$src/sys/arch/arm/sunxi"
awk '$2=="LRADC_BGR_REG" || /SUNXI_CCU_RESET\(A100_RST_BUS_LRADC,/ || /SUNXI_CCU_GATE\(A100_CLK_BUS_LRADC,/' \
    "$sunxi/sun50i_a100_ccu.c" > "$tmp/keys-ccu.h"
power="$src/sys/dev/sysmon/sysmon_power.c"
for fn in sysmon_pswitch_dispatch sysmon_pmf_hotkey_press; do
    case "$fn" in
        sysmon_pswitch_dispatch) printf 'static bool\n' ;;
        *) printf 'void\n' ;;
    esac
    sed -n "/^${fn}(/,/^}/p" "$power"
done > "$tmp/keys-dispatch.h"
for fn in sunxi_lradc_volume_press sunxi_lradc_volume_release; do
    printf 'static void\n'
    sed -n "/^${fn}(/,/^}/p" "$sunxi/sunxi_lradc.c"
done > "$tmp/keys-callbacks.h"
sed -n '/^sunxi_lradc_volume_intr(/,/^}/p' "$sunxi/sunxi_lradc.c" |
    { printf 'static int\n'; cat; } > "$tmp/keys-intr.h"
sed -n '/^static const struct device_compatible_entry volume_compat\[\]/,/^};/p;
    /^static const struct device_compatible_entry compat_data\[\]/,/^};/p' \
    "$sunxi/sunxi_lradc.c" > "$tmp/keys-match.h"
sed -n '/^sunxi_lradc_match(/,/^}/p' "$sunxi/sunxi_lradc.c" |
    { printf 'static int\n'; cat; } >> "$tmp/keys-match.h"
"$CC" -std=c99 -Wall -Wextra -Werror ${CPPFLAGS:-} -I"$src/sys/arch" -I"$tmp" \
    "$tools/a133-keys-fdt-test.c" "$sunxi/sun50i_a133_fdt.c" ${LDFLAGS:-} -lfdt -o "$tmp/test"
"$tmp/test"
