#!/bin/sh
# Exercise the actual platform gate before the module maps BCM2712 GPIO.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rpi5button.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int supported(const char *product)
{
C
awk '/if \(product == NULL/ {copy=1} copy {print}
copy && /return ENXIO;/ {exit}' "$src/sys/modules/rpi5button/rpi5button.c" >> "$work/check.c"
cat >> "$work/check.c" <<'C'
return 0;
}
int main(void)
{
    assert(supported("Raspberry Pi 5 Model B") == 0);
    assert(supported("Raspberry Pi Compute Module 5") == 0);
    assert(supported(NULL) == ENXIO);
    assert(supported("Raspberry Pi 4 Model B") == ENXIO);
    assert(supported("Raspberry Pi Compute Module 4") == ENXIO);
    assert(supported("Raspberry Pi Compute Module 5 unknown") == ENXIO);
    puts("PASS: exact Pi 5/CM5 platform gate; foreign boards rejected");
    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/check.c" -o "$work/check"
"$work/check"
