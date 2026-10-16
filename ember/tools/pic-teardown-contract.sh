#!/bin/sh
# Origin: EmberBSD (AI-assisted). Compile actual PIC teardown helpers.
set -eu
[ "$#" = 2 ] || { echo "Usage: $0 PIC_C NEW_WORK" >&2; exit 2; }
source=$1
work=$2
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$work"
awk '
	$0 ~ /^pic_blocked_enable_mask\(/ {print prev; copy=1}
	copy {print}
	copy && /^}/ {exit}
	{prev=$0}
' "$source" > "$work/pic_blocked_enable_mask.h"
test -s "$work/pic_blocked_enable_mask.h"
awk '
	$0 ~ /^pic_disestablish_source\(/ {print prev; copy=1}
	copy {print}
	copy && /^}/ {exit}
	{prev=$0}
' "$source" > "$work/pic_disestablish_source.h"
test -s "$work/pic_disestablish_source.h"
${CC:-cc} ${CFLAGS:--O1 -g -Wall -Wextra -Werror} -I "$work" \
    "$here/pic-teardown-contract.c" -o "$work/check"
"$work/check"
