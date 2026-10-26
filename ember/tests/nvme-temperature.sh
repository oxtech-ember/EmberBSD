#!/bin/sh
# Execute the production NVMe composite temperature sensor functions with a
# fake admin queue, then check that deliberate faults in them are caught.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/nvme-temperature.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=${1:-$src/sys/dev/ic/nvme.c}
regs=$src/sys/dev/ic/nvmereg.h
grep -E '^#define[[:space:]]+(NVM_ADMIN_GET_LOG_PG|NVM_LOG_PAGE_SMART_HEALTH|NVM_LOG_PAGE_SMART_HEALTH_LEN|NVM_SMART_COMPOSITE_TEMP)[[:space:]]' "$regs" > "$work/nvme-temperature-defs.h"
grep -E '^#define[[:space:]]+NVME_TIMO_LOG[[:space:]]' "$input" >> "$work/nvme-temperature-defs.h"
[ "$(wc -l < "$work/nvme-temperature-defs.h")" -eq 5 ]
awk '/^(nvme_sensors_attach|nvme_sensors_detach|nvme_smart_temperature|nvme_sensors_refresh|nvme_sensors_get_limits)\(/ { print prev; copy = 1; n++ }
copy { print }
copy && /^}/ { copy = 0 }
{ prev = $0 }
END { if (n != 5) exit 1 }' "$input" > "$work/nvme-temperature.h"
build() {
	${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter \
	    -I"$work" "$src/ember/tools/nvme-temperature-contract.c" \
	    ${LDFLAGS:-} -o "$1"
}
build "$work/check"
${TEST_RUNNER:-} "$work/check"
# Each mutation must make the contract fail.
cp "$work/nvme-temperature.h" "$work/orig.h"
caught=0
for m in 's/NVM_LOG_PAGE_SMART_HEALTH_LEN \/ 4 - 1/NVM_LOG_PAGE_SMART_HEALTH_LEN \/ 4/' \
    's/nvme_ccb_get(sc->sc_admin_q, false)/nvme_ccb_get(sc->sc_admin_q, true)/' \
    's/k == 0 || k > 1000/k > 1000/' \
    's/k == 0 || k > 1000/k == 0/' \
    's/htolem32(&sqe.nsid, 0xffffffff)/htolem32(\&sqe.nsid, 1)/' \
    's/if (rv != 0)$/if (rv < 0)/' \
    's/\*props |= PROP_WARNMAX;/*props = PROP_WARNMAX;/' \
    's/sysmon_envsys_destroy(sme);/(void)sme;/' \
    's/\(sysmon_envsys_unregister(sc->sc_sme);\)/\1 sysmon_envsys_destroy(sc->sc_sme);/' \
    's/if (rv == 1) {/if (0) {/' \
    's/ccb->ccb_done = nvme_empty_done;/;/' \
    's/if (sc->sc_temp_suspended || sc->sc_temp_dead)/if (sc->sc_temp_dead)/' \
    's/warn \* 1000000 - 1/warn * 1000000/'; do
	sed "$m" "$work/orig.h" > "$work/nvme-temperature.h"
	if cmp -s "$work/orig.h" "$work/nvme-temperature.h"; then
		echo "mutation did not apply: $m" >&2
		exit 1
	fi
	build "$work/mutant"
	if ${TEST_RUNNER:-} "$work/mutant" > /dev/null 2>&1; then
		echo "mutation not caught: $m" >&2
		exit 1
	fi
	caught=$((caught + 1))
done
echo "nvme temperature mutations caught: $caught"
