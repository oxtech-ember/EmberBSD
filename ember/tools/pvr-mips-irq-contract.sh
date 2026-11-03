#!/bin/sh
# SPDX-License-Identifier: MIT
# Origin: EmberBSD; source-only MIPS IRQ contract.
set -eu
: "${CC:?Select the native SDK compiler}"
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/pvr-mips-irq.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d' "$src/sys/dev/powervr/pvr_mips_irq.c" > "$work/irq-actual.h"
"$CC" -std=c11 -Wall -Wextra -Werror -I"$work" "$src/ember/tools/pvr-mips-irq-contract.c" -o "$work/test"
"$work/test"
