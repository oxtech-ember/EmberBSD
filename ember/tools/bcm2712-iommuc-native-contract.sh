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
    native-types:bcm2712_iommuc_native.h native:bcm2712_iommuc_native.c; do
    name=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/$input" > "$work/$name.h"
done
# This slice must not acquire a backend, map or touch hardware.
if grep -E 'bus_space_|core_(init|enable_flush|reserve|retain|release|fini)\(' \
    "$work/native.h" | grep -v 'bus_space_tag_t'; then
    echo 'ICAC native descriptor slice contains an operational backend' >&2
    exit 1
fi
compile()
{
    # CFLAGS is an explicit compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -I"$acpica" -I"$work" -MD -MF "$work/contract.d" \
        "$src/ember/tools/bcm2712-iommuc-native-contract.c" -o "$work/contract"
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
    ICAC_SOURCE="$src" ICAC_SAVED="$ICAC_NATIVE_EVIDENCE" ruby -rjson -rdigest -e '
        root=ENV.fetch("ICAC_SOURCE"); output=ENV.fetch("ICAC_SAVED")
        paths=%w[bcm2712_iommuc_corevar.h bcm2712_iommuc_core.c bcm2712_iommuc_native.h bcm2712_iommuc_native.c].map { |p| root+"/sys/arch/arm/broadcom/"+p }
        paths += %w[bcm2712-iommuc-native-contract.c bcm2712-iommuc-native-contract.sh].map { |p| root+"/ember/tools/"+p }
        receipt={source_sha256:paths.to_h { |p| [p,Digest::SHA256.file(p).hexdigest] },extraction:"Only include/RCS lines removed from complete actual source/header files. Query fixture includes both actual query TUs and the real bounded ACPICA helper.",scope:"Controlled ACPI/device/mutex/once/log services; no operational backend or hardware. Private fixture-only core initialization exercises cached state without callbacks."}
        File.write(output+"/source.json",JSON.pretty_generate(receipt)+"\n")'
fi
if [ "${ICAC_NATIVE_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${ICAC_NATIVE_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/native.h" "$work/native-good.h"
ulimit -c 0
for mutant in ${ICAC_NATIVE_MUTANTS:-context node-type node-handle driver owner attached device handle tag version address length status-zero status-attached status-core duplicate detach}; do
    cp "$work/native-good.h" "$work/native.h"
    ruby - "$work/native.h" "$mutant" <<'RUBY'
path,name=ARGV; s=File.read(path)
pairs={
  'context'=>["\tif (handle == NULL || out == NULL)\n\t\treturn EINVAL;\n\tif ((error = bcm2712_iommuc_native_context()) != 0)\n\t\treturn error;", "\tif (handle == NULL || out == NULL)\n\t\treturn EINVAL;"],
  'node-type'=>['node == NULL || node->ad_type != ACPI_TYPE_DEVICE','node == NULL || false'],
  'node-handle'=>['node->ad_handle != handle','false'],
  'driver'=>['!device_is_a(node->ad_device, "bcm2712iommuc")','(!device_is_a(node->ad_device, "bcm2712iommuc") && false)'],
  'owner'=>['sc == NULL || sc != bcm2712_iommuc_owner ||','sc == NULL ||'],
  'attached'=>['!sc->software_attached || sc->dev','false || sc->dev'],
  'device'=>['sc->dev != node->ad_device','false'],
  'handle'=>['sc->handle != handle','false'],
  'tag'=>['sc->bst == NULL','false'],
  'version'=>['sc->contract.version != contract.version','false'],
  'address'=>['sc->contract.address != contract.address','false'],
  'length'=>['sc->contract.length != contract.length','false'],
  'status-zero'=>['memset(&status, 0, sizeof(status));','memset(&status, 0xa5, sizeof(status));'],
  'status-attached'=>['status.software_attached = true;','status.software_attached = false;'],
  'status-core'=>['status.core_initialized = sc->core.initialized;','status.core_initialized = false;'],
  'duplicate'=>['if (bcm2712_iommuc_owner != NULL) {','if (bcm2712_iommuc_owner != NULL && false) {'],
  'detach'=>['return EBUSY;','return 0;']
}
before,after=pairs.fetch(name)
abort "mutant anchor mismatch: #{name}" unless s.scan(before).length==1
File.write(path,s.sub(before,after))
RUBY
    compile
    if run >"$work/mutant-$mutant.log" 2>&1; then
        echo "ICAC native contract accepted unsafe mutant: $mutant" >&2
        exit 1
    fi
    if [ -n "${ICAC_NATIVE_EVIDENCE:-}" ]; then
        cp "$work/native.h" "$ICAC_NATIVE_EVIDENCE/mutant-$mutant.h"
        cp "$work/mutant-$mutant.log" "$ICAC_NATIVE_EVIDENCE/"
    fi
    echo "BCM2712 ICAC native contract: rejected $mutant mutant"
done
