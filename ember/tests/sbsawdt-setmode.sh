#!/bin/sh
# Execute the production SBSA watchdog setmode and tickle functions with a
# recorded register bus, then check that deliberate faults are caught.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/sbsawdt-setmode.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=${1:-$src/sys/arch/arm/acpi/sbsawdt_acpi.c}
awk '/^#define[[:space:]]+[RC]_[A-Z0-9_]+_REG[[:space:]]/ || /^#define[[:space:]]+C_WCS_/ { print; next }
/^#define[[:space:]]+(REFRESH|CONTROL)_(RD4|WR4)\(/ { macro = 1 }
macro { print; if (!/\\$/) macro = 0; prev = $0; next }
/^struct sbsawdt_acpi_softc \{/ { copy = 1 }
/^(sbsawdt_acpi_refresh|sbsawdt_acpi_tickle|sbsawdt_acpi_setmode)\(/ { print prev; copy = 1; n++ }
copy { print }
copy && /^}/ { copy = 0 }
{ prev = $0 }
END { if (n != 3) exit 1 }' "$input" |
    sed -e 's/struct sysmon_wdog[[:space:]]*sc_smw;/struct sysmon_wdog *sc_smw_unused;/' > "$work/sbsawdt-setmode.h"
build() {
	${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
	    -Wno-unused-macros -I"$work" \
	    "$src/ember/tools/sbsawdt-setmode-contract.c" ${LDFLAGS:-} -o "$1"
}
build "$work/check"
${TEST_RUNNER:-} "$work/check"
cp "$work/sbsawdt-setmode.h" "$work/orig.h"
caught=0
for m in 's/(uint64_t)smw->smw_period/smw->smw_period/' \
    's/if (wor > UINT32_MAX)/if (0)/' \
    's/sc->sc_cntfreq \/ 2/sc->sc_cntfreq/' \
    's/CONTROL_WR4(sc, C_WCS_REG, C_WCS_EN);/CONTROL_WR4(sc, C_WCS_REG, 0);/' \
    '/^sbsawdt_acpi_refresh(/,/^}/s/CONTROL_WR4(sc, C_WOR_REG, sc->sc_wor);/;/' \
    '/^sbsawdt_acpi_refresh(/,/^}/s/REFRESH_WR4(sc, R_WRR_REG, 0);/;/' \
    's/sc->sc_wor = (uint32_t)wor;/;/'; do
	sed "$m" "$work/orig.h" > "$work/sbsawdt-setmode.h"
	if cmp -s "$work/orig.h" "$work/sbsawdt-setmode.h"; then
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
echo "sbsawdt setmode mutations caught: $caught"
