#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-iommuc-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'BCM2712 ICAC temporary directory: %s\n' "$work"
acpica=${ACPICA_INCLUDE:-$src/sys/external/bsd/acpica/dist/include}
for pair in display:bcm2712_display.h iommuc:bcm2712_iommuc.h \
    display-core:bcm2712_display.c display-adapter:bcm2712_display_acpi.c \
    iommuc-adapter:bcm2712_iommuc_acpi.c; do
    name=${pair%%:*}; input=${pair#*:}
    sed '/^#include /d; /^__KERNEL_RCSID(/d' \
        "$src/sys/arch/arm/broadcom/$input" > "$work/$name.h"
done
# Keep both actual query TUs in the existing causal fixture.
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_iommuc_dependency.c" >> "$work/iommuc-adapter.h"
# Keep the exact complete BSD function, including its return type and license.
awk '/^#include/ { exit } { print }' "$src/sys/dev/acpi/acpi_util.c" > "$work/reference-helper.h"
awk '
    /^acpi_eval_reference_handle\(/ { print previous; copying=1; found++ }
    copying { print; if ($0 == "}") copying=0 }
    { previous=$0 }
    END { if (found != 1 || copying) exit 1 }
' "$src/sys/dev/acpi/acpi_util.c" >> "$work/reference-helper.h"
awk '/^#define[ \t]+(VCPROP_CLK_M2MC|VCPROP_CLK_PIXEL_BVB|VCPROP_CLK_CORE|VCPROP_CLK_DISP|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
compile()
{
    # CFLAGS is an explicit compiler argument list.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -I"$acpica" -I"$work" -MD -MF "$work/contract.d" \
        "$src/ember/tools/bcm2712-iommuc-contract.c" -o "$work/contract"
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
if [ -n "${ICAC_OUTPUT:-}" ]; then cp "$work/contract" "$ICAC_OUTPUT"; fi
if [ -n "${ICAC_EVIDENCE:-}" ]; then
    mkdir -p "$ICAC_EVIDENCE"
    cp "$work/"*.h "$work/contract.d" "$ICAC_EVIDENCE/"
    cp "$src/ember/tools/bcm2712-iommuc-contract.c" "$ICAC_EVIDENCE/fixture.c"
    ICAC_SOURCE="$src" ICAC_HEADERS="$acpica" ICAC_SAVED="$ICAC_EVIDENCE" ruby -rjson -rdigest -e '
        root=ENV.fetch("ICAC_SOURCE"); output=ENV.fetch("ICAC_SAVED")
        paths=%w[bcm2712_display.h bcm2712_display.c bcm2712_display_acpi.c bcm2712_iommuc.h bcm2712_iommuc_acpi.c bcm2712_iommuc_dependency.c].map { |p| root+"/sys/arch/arm/broadcom/"+p }
        paths += [root+"/sys/dev/acpi/acpi_util.c",root+"/ember/tools/bcm2712-iommuc-contract.c",root+"/sys/arch/evbarm/rpi/vcprop.h"]
        paths += %w[actypes.h acrestyp.h acexcep.h platform/acenv.h].map { |p| ENV.fetch("ICAC_HEADERS")+"/"+p }
        receipt={source_sha256:paths.to_h { |p| [p,Digest::SHA256.file(p).hexdigest] },extraction:"Only include/RCS lines removed from actual query/core sources; complete unchanged reference helper plus original license. Real ACPICA public type headers.",scope:"Controlled namespace services only; no provider/readiness/permission or MMIO model."}
        File.write(output+"/source.json",JSON.pretty_generate(receipt)+"\n")'
fi
if [ "${ICAC_COMPILE_ONLY:-0}" = 1 ]; then exit 0; fi
run
if [ "${ICAC_SKIP_MUTANTS:-0}" = 1 ]; then exit 0; fi
cp "$work/iommuc-adapter.h" "$work/iommuc-good.h"
cp "$work/iommuc.h" "$work/iommuc-types-good.h"
ulimit -c 0
for mutant in ${ICAC_MUTANTS:-version address length maximum consumer caching sta uid reference-type actual-type duplicate missing-cache gpu-identity gpu-contract padding output limit}; do
    cp "$work/iommuc-good.h" "$work/iommuc-adapter.h"
    cp "$work/iommuc-types-good.h" "$work/iommuc.h"
    ruby - "$work" "$mutant" <<'RUBY'
work,name=ARGV
path=work+'/iommuc-adapter.h'
s=File.read(path)
pairs={
  'version'=>['value != BCM2712_IOMMUC_VERSION','value == 0'],
  'address'=>['address->Address.Minimum != BCM2712_IOMMUC_ADDRESS','false'],
  'length'=>['address->Address.AddressLength != BCM2712_IOMMUC_LENGTH','false'],
  'maximum'=>["address->Address.Maximum != BCM2712_IOMMUC_ADDRESS +\n\t\t    BCM2712_IOMMUC_LENGTH - 1",'false'],
  'consumer'=>['address->ProducerConsumer != ACPI_CONSUMER','false'],
  'caching'=>['address->Info.Mem.Caching != ACPI_NON_CACHEABLE_MEMORY','false'],
  'sta'=>['value != 0x0f','false'],
  'uid'=>['value != 0 ||','false ||'],
  'reference-type'=>['element->Type != ACPI_TYPE_LOCAL_REFERENCE','false'],
  'actual-type'=>['element->Reference.ActualType != ACPI_TYPE_DEVICE','false'],
  'duplicate'=>['seen[j] == handle','seen[j] == handle && false'],
  'missing-cache'=>['matches != 1','matches > 1'],
  'gpu-identity'=>['gpu != handle','false'],
  'gpu-contract'=>["error = bcm2712_display_acpi_resources(NULL, gpu, &display);","error = bcm2712_display_acpi_resources(NULL, gpu, &display);\n\terror = 0;"],
  'padding'=>['memset(&decoded, 0, sizeof(decoded));','memset(&decoded, 0xa5, sizeof(decoded));'],
  'output'=>['*out = decoded;','*out = decoded; out->length = 0;']
}
if name=='limit'
  path=work+'/iommuc.h'; s=File.read(path)
  before,after=['#define BCM2712_IOMMUC_MAX_DEPENDENCIES 32','#define BCM2712_IOMMUC_MAX_DEPENDENCIES 31']
else
  before,after=pairs.fetch(name)
end
# output is deliberately tested at both identical commit sites.
expected=name=='output' ? 2 : 1
abort "mutant anchor mismatch: #{name}" unless s.scan(before).length==expected
File.write(path,s.gsub(before,after))
RUBY
    compile
    if run >"$work/mutant-$mutant.log" 2>&1; then
        echo "ICAC contract accepted unsafe mutant: $mutant" >&2
        exit 1
    fi
    if [ -n "${ICAC_EVIDENCE:-}" ]; then
        cp "$work/iommuc-adapter.h" "$ICAC_EVIDENCE/mutant-$mutant.h"
        cp "$work/iommuc.h" "$ICAC_EVIDENCE/mutant-$mutant-types.h"
        cp "$work/mutant-$mutant.log" "$ICAC_EVIDENCE/"
    fi
    echo "BCM2712 ICAC contract: rejected $mutant mutant"
done
