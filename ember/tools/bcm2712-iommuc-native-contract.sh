#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-iommuc-native-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'BCM2712 ICAC native temporary directory: %s\n' "$work"
acpica=${ACPICA_INCLUDE:-$src/sys/external/bsd/acpica/dist/include}
ICAC_COMPILE_ONLY=1 ICAC_EVIDENCE="$work/query" \
    sh "$src/ember/tools/bcm2712-iommuc-contract.sh" "$src"
cp "$work/query/"*.h "$work/"
for pair in core-types:bcm2712_iommuc_corevar.h core:bcm2712_iommuc_core.c \
    native-types:bcm2712_iommuc_native.h native:bcm2712_iommuc_native.c \
    executor-types:bcm2712_iommuc_executor.h mmu4-types:bcm2712_mmu4_corevar.h \
    mmu4:bcm2712_mmu4_core.c; do
    name=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/$input" > "$work/$name.h"
done
# No unmap or public lease/flush/close escape exists in the native owner.
if grep -E 'bus_space_unmap|core_(enable_flush|reserve|retain|release|fini)\(' \
    "$work/native.h" | grep -v 'bus_space_tag_t'; then
    echo 'ICAC native owner contains a forbidden unmap or shared lifecycle escape' >&2
    exit 1
fi
# Reuse the complete existing backing/helper/encoder fixture export. Do not run
# its independent matrix here. The separate bridge TU uses its service doubles.
BACKING_COMPILE_ONLY=1 BACKING_PAGES=4096 BACKING_EVIDENCE="$work/backing" \
    ruby "$src/ember/tools/bcmv3d-mmu4-backing-contract.rb" "$src"
for input in bcmv3d_mmu4_backing.c bcmv3d_hvs_prepare.c arm/broadcom/bcm2712_mmu4_table.c; do
    object="$work/$(basename "$input" .c).o"
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -I"$work/backing" -MD -MF "$object.d" \
        -c "$work/backing/$input" -o "$object"
done
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work/backing" -I"$src/ember/tools" -MD -MF "$work/backing-bridge.d" \
    -c "$src/ember/tools/bcm2712-iommuc-executor-backing.c" -o "$work/backing-bridge.o"
compile()
{
    # CFLAGS is an explicit compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -I"$acpica" -I"$work" -MD -MF "$work/contract.d" \
        "$src/ember/tools/bcm2712-iommuc-native-contract.c" \
        "$work/bcmv3d_mmu4_backing.o" "$work/bcmv3d_hvs_prepare.o" \
        "$work/bcm2712_mmu4_table.o" "$work/backing-bridge.o" -o "$work/contract"
}
run()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$work/contract"
    else
        "$work/contract"
    fi
}
compile
if [ -n "${ICAC_NATIVE_OUTPUT:-}" ]; then cp "$work/contract" "$ICAC_NATIVE_OUTPUT"; fi
if [ -n "${ICAC_NATIVE_EVIDENCE:-}" ]; then
    mkdir -p "$ICAC_NATIVE_EVIDENCE"
    cp "$work/"*.h "$work/contract.d" "$ICAC_NATIVE_EVIDENCE/"
    cp "$src/ember/tools/bcm2712-iommuc-native-contract.c" "$ICAC_NATIVE_EVIDENCE/fixture.c"
    cp "$work/query/source.json" "$ICAC_NATIVE_EVIDENCE/query-source.json"
    cp "$work/backing/inputs.json" "$ICAC_NATIVE_EVIDENCE/backing-source.json"
    cp -R "$work/backing" "$ICAC_NATIVE_EVIDENCE/backing"
    ICAC_SOURCE="$src" ICAC_SAVED="$ICAC_NATIVE_EVIDENCE" ruby -rjson -rdigest -e '
        root=ENV.fetch("ICAC_SOURCE"); output=ENV.fetch("ICAC_SAVED")
        paths=%w[bcm2712_iommuc_corevar.h bcm2712_iommuc_core.c bcm2712_iommuc_native.h bcm2712_iommuc_native.c bcm2712_iommuc_executor.h bcm2712_mmu4_core.c bcm2712_mmu4_corevar.h].map { |p| root+"/sys/arch/arm/broadcom/"+p }
        paths += %w[bcm2712-iommuc-native-contract.c bcm2712-iommuc-native-contract.sh bcm2712-iommuc-executor-backing.c].map { |p| root+"/ember/tools/"+p }
        receipt={source_sha256:paths.to_h { |p| [p,Digest::SHA256.file(p).hexdigest] },extraction:"Only include/RCS lines removed from complete actual source/header files. Query fixture includes both actual query TUs and the real bounded ACPICA helper.",scope:"Controlled ACPI/device/mutex/once/log and fixed native bus map/read/write/barrier services; no hardware. The full existing backing service bridge controls allocation/BO/DMA/pmap services. A wrapper instruments the unchanged actual cache init and separately injects a synthetic init error. local_locked is a controlled external caller-lock precondition, not a native owner lock implementation. Private core init also exercises cached state without callbacks. Each reset starts a fresh simulated boot; there is no ICAC unmap/free route. Sealed backing stays retained in child simulated boots."}
        File.write(output+"/source.json",JSON.pretty_generate(receipt)+"\n")'
fi
if [ "${ICAC_NATIVE_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${ICAC_NATIVE_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/native.h" "$work/native-good.h"
ulimit -c 0
for mutant in ${ICAC_NATIVE_MUTANTS:-context node-type node-handle driver owner attached device handle tag version address length status-zero status-attached status-core duplicate detach observe-once observe-latch observe-active observe-address observe-length observe-offset observe-extra-read observe-attempt observe-mapped observe-raw observe-map-error observe-busy observe-unknown exec-once exec-latch exec-error exec-reentry exec-native-fault exec-core-fault exec-write-value exec-io-identity exec-foreign-lease exec-retained-error exec-barrier exec-write-offset}; do
    cp "$work/native-good.h" "$work/native.h"
    ruby - "$work/native.h" "$mutant" <<'RUBY'
path,name=ARGV; s=File.read(path)
pairs={
  'context'=>["\tif (handle == NULL || out == NULL)\n\t\treturn EINVAL;\n\tif ((error = bcm2712_iommuc_entry()) != 0)\n\t\treturn error;", "\tif (handle == NULL || out == NULL)\n\t\treturn EINVAL;"],
  'node-type'=>['node == NULL || node->ad_type != ACPI_TYPE_DEVICE','node == NULL || false'],
  'node-handle'=>['node->ad_handle != handle','false'],
  'driver'=>['!device_is_a(node->ad_device, "bcm2712iommuc")','(!device_is_a(node->ad_device, "bcm2712iommuc") && false)'],
  'owner'=>['sc == NULL || sc != bcm2712_iommuc_owner ||','sc == NULL ||'],
  'attached'=>['!sc->software_attached || sc->dev','false || sc->dev'],
  'device'=>['sc->dev != node->ad_device','false'],
  'handle'=>['sc->handle != handle','false'],
  'tag'=>['sc->handle != handle || sc->bst == NULL','sc->handle != handle || false'],
  'version'=>['sc->contract.version != contract.version','false'],
  'address'=>['sc->contract.address != contract.address','false'],
  'length'=>['sc->contract.length != contract.length','false'],
  'status-zero'=>['memset(&status, 0, sizeof(status));','memset(&status, 0xa5, sizeof(status));'],
  'status-attached'=>['status.software_attached = true;','status.software_attached = false;'],
  'status-core'=>['status.core_initialized = sc->core.initialized;','status.core_initialized = false;'],
  'duplicate'=>['if (bcm2712_iommuc_owner != NULL) {','if (bcm2712_iommuc_owner != NULL && false) {'],
'detach'=>['return EBUSY;','return 0;'],
  'observe-once'=>['if (observation->started) {','if (observation->started && false) {'],
  'observe-latch'=>['observation->started = true;','observation->started = false;'],
  'observe-active'=>["/* Do not introduce a new observation into a future active core. */\n\tif (sc->core.initialized) {","/* Do not introduce a new observation into a future active core. */\n\tif (sc->core.initialized && false) {"],
  'observe-address'=>['bus_space_map(sc->bst, BCM2712_IOMMUC_ADDRESS,','bus_space_map(sc->bst, BCM2712_IOMMUC_ADDRESS + 4,'],
  'observe-length'=>['BCM2712_IOMMUC_LENGTH, 0, &sc->bsh);','BCM2712_IOMMUC_LENGTH - 4, 0, &sc->bsh);'],
  'observe-offset'=>['value = bus_space_read_4(sc->bst, sc->bsh, 0);','value = bus_space_read_4(sc->bst, sc->bsh, 4);'],
  'observe-extra-read'=>['value = bus_space_read_4(sc->bst, sc->bsh, 0);',"value = bus_space_read_4(sc->bst, sc->bsh, 0);\n\tvalue = bus_space_read_4(sc->bst, sc->bsh, 0);"],
  'observe-attempt'=>['observation->reads_attempted = 1;','observation->reads_attempted = 0;'],
  'observe-mapped'=>['observation->mapped = true;','observation->mapped = false;'],
  'observe-raw'=>['observation->raw_control = value;','observation->raw_control = value ^ 1;'],
  'observe-map-error'=>["\t\tobservation->first_error = error;\n\t\tgoto done;", "\t\tobservation->first_error = 0;\n\t\tgoto done;"],
  'observe-busy'=>['value & IOMMUC_CONTROL_FLUSHING','value & 0'],
  'observe-unknown'=>['if ((value & IOMMUC_CONTROL_FLUSHING) != 0)', 'if ((value & ~7U) != 0) return EOPNOTSUPP; if ((value & IOMMUC_CONTROL_FLUSHING) != 0)'],
  'exec-once'=>['if (sc->mmu4.attempted) {','if (sc->mmu4.attempted && false) {'],
  'exec-latch'=>['sc->mmu4.attempted = true;','sc->mmu4.attempted = false;'],
  'exec-error'=>['sc->mmu4.first_error = error;','sc->mmu4.first_error = 0;'],
  'exec-reentry'=>['if (mutex_owned(&bcm2712_iommuc_mutex))','if (mutex_owned(&bcm2712_iommuc_mutex) && false)'],
  'exec-native-fault'=>["\tif (sc->mmu4.fault != 0)\n\t\treturn EIO;", "\tif (sc->mmu4.fault != 0 && false)\n\t\treturn EIO;"],
  'exec-core-fault'=>['if (sc->core.status.fault != 0) {','if (sc->core.status.fault != 0 && false) {'],
  'exec-write-value'=>['if (value != 3U)','if (value != 3U && false)'],
  'exec-io-identity'=>["if (!bcm2712_iommuc_callbacks(sc) ||\n\t    sc->core.status.stage == BCM2712_IOMMUC_CLOSED)","if (false ||\n\t    sc->core.status.stage == BCM2712_IOMMUC_CLOSED)"],
  'exec-foreign-lease'=>["(before.reserved & ~IOMMUC_MMU4_MEMBER) !=\n\t    (after.reserved & ~IOMMUC_MMU4_MEMBER)",'false'],
  'exec-retained-error'=>['if (error != 0 && ((after.reserved | after.retained) &','if (error != 0 && false && ((after.reserved | after.retained) &'],
  'exec-barrier'=>['BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);','BUS_SPACE_BARRIER_WRITE);'],
  'exec-write-offset'=>['bus_space_write_4(sc->bst, sc->bsh, 0, value);','bus_space_write_4(sc->bst, sc->bsh, 4, value);']
}
before,after=pairs.fetch(name)
abort "mutant anchor mismatch: #{name}" unless s.scan(before).length==1
File.write(path,s.sub(before,after))
RUBY
    compile
    set +e
    run >"$work/mutant-$mutant.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 134 ] || ! grep -q 'ICAC contract failed at line ' "$work/mutant-$mutant.log"; then
        echo "ICAC mutant did not fail by CHECK/SIGABRT: $mutant (status $status)" >&2
        cat "$work/mutant-$mutant.log" >&2
        exit 1
    fi
    if [ -n "${ICAC_NATIVE_EVIDENCE:-}" ]; then
        cp "$work/native.h" "$ICAC_NATIVE_EVIDENCE/mutant-$mutant.h"
        cp "$work/mutant-$mutant.log" "$ICAC_NATIVE_EVIDENCE/"
    fi
    echo "BCM2712 ICAC native contract: rejected $mutant mutant"
done
