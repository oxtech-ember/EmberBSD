#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; native IRQ handler source contract.
set -eu
: "${CC:?Select the native SDK compiler}"
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a133-gpu-irq.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for name in a133_gpu_interrupt a133_gpu_irq_arm a133_gpu_queue_run; do
 printf 'static int\n' >> "$work/irq-driver.inc"
 sed -n "/^$name(/,/^}/p" "$src/sys/arch/arm/sunxi/sun50i_a133_gpu.c" >> "$work/irq-driver.inc"
done
"$CC" -std=c11 -Wall -Wextra -Werror -I"$work" "$src/ember/tools/a133-gpu-irq-contract.c" -o "$work/test"
"$work/test"
