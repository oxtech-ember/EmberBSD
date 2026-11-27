#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
# Exercise the production sleep state mask of acpi(4) on a development
# host: hw.acpi.sleep.disabled removes S1-S4 and keeps S0 and S5.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
acpi=$here/../../sys/dev/acpi
work=$(mktemp -d "${TMPDIR:-/tmp}/acpi-sleep-disabled.XXXXXX")
trap 'rm -rf "$work"' EXIT
awk '
    /^acpi_sleep_available\(/ {print prev; copy=1}
    copy {print}
    copy && /^}/ {exit}
    {prev=$0}
' "$acpi/acpi.c" > "$work/acpi_sleep_available.inc"
grep -q '^acpi_sleep_available(' "$work/acpi_sleep_available.inc"
grep -q '"disabled", SYSCTL_DESCR' "$acpi/acpi.c"
grep -q 'acpi_sleep_available(sc) & __BIT(state)' "$acpi/acpi.c"
grep -q 'states = acpi_sleep_available(sc);' "$acpi/acpi.c"
grep -q 'acpi_sleep_available(sc) & __BIT(t)) == 0)' "$acpi/acpi.c"
cat > "$work/check.c" <<'EOC'
#include <assert.h>
#include <stdio.h>
#define __BIT(n) (1 << (n))
enum { ACPI_STATE_S0, ACPI_STATE_S1, ACPI_STATE_S2, ACPI_STATE_S3,
    ACPI_STATE_S4, ACPI_STATE_S5 };
struct acpi_softc { int sc_sleepstates; };
#define atomic_load_relaxed(p) (*(p))
static int acpi_sleep_disabled;
#include "acpi_sleep_available.inc"
int
main(void)
{
	/* QBOOK15 firmware: S0 S3 S4 S5. */
	struct acpi_softc sc = { __BIT(0) | __BIT(3) | __BIT(4) | __BIT(5) };
	struct acpi_softc all = { 0x3f };

	assert(acpi_sleep_available(&sc) == sc.sc_sleepstates);
	acpi_sleep_disabled = 1;
	assert(acpi_sleep_available(&sc) == (__BIT(0) | __BIT(5)));
	assert(acpi_sleep_available(&all) == (__BIT(0) | __BIT(5)));
	acpi_sleep_disabled = 0;
	assert(acpi_sleep_available(&all) == 0x3f);
	printf("acpi-sleep-disabled: ok\n");
	return 0;
}
EOC
${CC:-cc} ${CFLAGS:--O2 -g} -std=gnu11 -Wall -Wextra -Werror -UNDEBUG \
    -I "$work" "$work/check.c" -o "$work/check"
"$work/check"
