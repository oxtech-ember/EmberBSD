#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -r "$work"' EXIT
: "${CC:=cc}"
sed '/^#include /d' "$src/sys/dev/powervr/pvr_job_irq.h" > "$work/job-irq-actual.h"
sed '/^#include /d' "$src/sys/dev/powervr/pvr_job_irq.c" >> "$work/job-irq-actual.h"
"$CC" -std=c11 -Wall -Wextra -Werror -I"$work" "$src/ember/tools/pvr-job-irq-contract.c" -o "$work/contract"
"$work/contract"
