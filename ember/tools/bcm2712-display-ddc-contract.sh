#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-ddc-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'BCM2712 DDC temporary directory: %s\n' "$work"
mkdir -p "$work/include/dev/i2c" "$work/include/sys" "$work/include/prop"
for file in i2cvar.h i2c_io.h brcmstb_i2creg.h brcmstb_i2cvar.h; do
    cp "$src/sys/dev/i2c/$file" "$work/include/dev/i2c/"
done
awk '/^#define[ \t]+__BIT\(/ { copying=1 } copying { print; if (substr($0,length($0)) != sprintf("%c",92)) copying=0 }' \
    "$src/sys/sys/cdefs.h" > "$work/include/bsd-bit.h"
cat > "$work/include/sys/device.h" <<'HEADER'
#include <stdbool.h>
#include <stdint.h>
#include <sys/queue.h>
typedef void *device_t;
typedef void *cfdata_t;
struct device_compatible_entry;
HEADER
cat > "$work/include/sys/mutex.h" <<'HEADER'
#ifndef FIXTURE_MUTEX_H
#define FIXTURE_MUTEX_H
#include <pthread.h>
#include <stdbool.h>
typedef struct { pthread_mutex_t native; pthread_t owner; bool tag, held; } kmutex_t;
#endif
HEADER
cat > "$work/include/prop/proplib.h" <<'HEADER'
typedef void *prop_array_t;
typedef void *prop_dictionary_t;
HEADER
for pair in adapter:bcm2712_display_ddc_acpi.c adapter-types:bcm2712_display_ddc.h \
    decoder:bcm2712_display.c display:bcm2712_display.h; do
    name=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/$input" > "$work/$name.h"
done
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_MAX_CLOCKRATE|VCPROPTAG_SET_CLOCKSTATE|VCPROPTAG_SET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
# Extract exact complete functions, including their return-type line. Do not
# rewrite dispatch or validation. Keep the source license in each extracted file.
extract()
{
    awk -v fn="$2" '
        $0 ~ "^" fn "\\(" { print previous; copying=1; found++ }
        copying { print; if ($0 == "}") copying=0 }
        { previous=$0 }
        END { if (found != 1 || copying) exit 1 }
    ' "$1"
}
awk '/^#include/ { exit } { print }' "$src/sys/dev/i2c/i2c_exec.c" > "$work/bsd-iic.h"
for fn in iic_smbus_crc8 iic_smbus_pec iic_op_flags iic_tag_init iic_tag_fini \
    iic_acquire_bus iic_release_bus iic_exec; do
    extract "$src/sys/dev/i2c/i2c_exec.c" "$fn" >> "$work/bsd-iic.h"
done
awk '/^#include/ { exit } { print }' "$src/sys/dev/videomode/edid.c" > "$work/bsd-edid.h"
awk '/^#define[ \t]+EDID_SIGNATURE[ \t]/ { print; found++ } END { if (found != 1) exit 1 }' \
    "$src/sys/dev/videomode/edidreg.h" >> "$work/bsd-edid.h"
extract "$src/sys/dev/videomode/edid.c" edid_is_valid >> "$work/bsd-edid.h"
cp "$src/sys/dev/i2c/brcmstb_i2c.c" "$work/core.c"
ulimit -c 0
# This boundary proxy records the actual iic request, then calls this unchanged core.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -Dbrcmstb_i2c_exec=fixture_core_exec -include "$work/include/bsd-bit.h" \
    -I"$work/include" -c "$work/core.c" -o "$work/core.o"
compile()
{
    # CFLAGS is a caller-supplied argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${CFLAGS:-} \
        -include "$work/include/bsd-bit.h" -I"$work/include" -I"$work" \
        "$src/ember/tools/bcm2712-display-ddc-contract.c" "$work/core.o" \
        -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/contract"; else "$work/contract"; fi
}
compile
if [ -n "${DDC_OUTPUT:-}" ]; then cp "$work/contract" "$DDC_OUTPUT"; fi
if [ -n "${DDC_EVIDENCE:-}" ]; then
    mkdir -p "$DDC_EVIDENCE"
    cp -R "$work/include" "$DDC_EVIDENCE/"
    cp "$work/"*.h "$work/core.c" "$src/ember/tools/bcm2712-display-ddc-contract.c" "$DDC_EVIDENCE/"
    if command -v sha256sum >/dev/null 2>&1; then hash=sha256sum
    elif command -v shasum >/dev/null 2>&1; then hash='shasum -a 256'
    else hash=sha256; fi
    # These are paths, not compiler input flags. Keep the origins and exact hashes.
    # shellcheck disable=SC2086
    $hash "$src/sys/dev/i2c/i2c_exec.c" "$src/sys/dev/videomode/edid.c" \
        "$src/sys/dev/videomode/edidreg.h" "$work/bsd-iic.h" "$work/bsd-edid.h" \
        > "$DDC_EVIDENCE/extraction-sha256.txt"
fi
if [ "${DDC_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${DDC_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/adapter.h" "$work/good.h"
for mutant in size tag-init cookie exec post-guard mask pending early-copy edid \
    native-fault destroy-fault release-acquire status-zero; do
    nth=1
    case "$mutant" in
        status-zero) from='memset(&status, 0, sizeof(status));'; to='memset(&status, 0xa5, sizeof(status));' ;;
        size) from='length != sizeof(data)'; to='length > sizeof(data)' ;;
        tag-init) from='sc->tag.ic_exec = brcmstb_i2c_exec;'; to='sc->tag.ic_exec = brcmstb_i2c_exec; iic_tag_init(&sc->tag);' ;;
        cookie) from='sc->tag.ic_cookie = &sc->core;'; to='sc->tag.ic_cookie = &sc->tag;' ;;
        exec) from='sc->tag.ic_exec = brcmstb_i2c_exec;'; to='sc->tag.ic_exec = NULL;' ;;
        post-guard) from='error = bcm2712_ddc_guard(sc, true);'; to='error = 0;'; nth=2 ;;
        mask) from='if ((sc->bsc_mask & 2U) == 0 ||'; to='if (false ||' ;;
        pending) from='(idle && (sc->bsc_status & 2U) != 0))'; to='false)' ;;
        early-copy) from='if (edid_is_valid(data) != 0) {'; to='memcpy(out, data, sizeof(data)); if (edid_is_valid(data) != 0) {' ;;
        edid) from='if (edid_is_valid(data) != 0) {'; to='if (false) {' ;;
        native-fault) from='sc->fault = error;'; to='sc->fault = 0;' ;;
        destroy-fault) from='sc->fault != 0 ? sc->fault : brcmstb_i2c_fini(&sc->core)'; to='brcmstb_i2c_fini(&sc->core)' ;;
        release-acquire) from='error = iic_acquire_bus(&sc->tag, I2C_F_POLL);'; to='error = iic_acquire_bus(&sc->tag, I2C_F_POLL); if (error != 0) iic_release_bus(&sc->tag, I2C_F_POLL);' ;;
        *) echo "Unknown DDC adapter mutant: $mutant" >&2; exit 1 ;;
    esac
    awk -v from="$from" -v to="$to" -v nth="$nth" '
        !changed { p=index($0,from); if (p && ++seen == nth) {
            $0=substr($0,1,p-1) to substr($0,p+length(from)); changed++
        } }
        { print } END { if (changed != 1) exit 1 }
    ' "$work/good.h" > "$work/adapter.h"
    compile
    if (run) >"$work/mutant.log" 2>&1; then
        echo "Surviving native DDC causal mutant: $mutant" >&2; exit 1
    fi
    printf 'Native DDC causal mutant rejected: %s\n' "$mutant"
done
