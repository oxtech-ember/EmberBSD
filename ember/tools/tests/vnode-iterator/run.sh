#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: Exercise the real vnode iterator with controlled list mutations.
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$dir/../../../.." && pwd)
source=${1:-"$root/sys/kern/vfs_mount.c"}
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-vnode-iterator.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
extract()
{
    awk -v name="$1" -v type="$2" '
        $0 ~ ("^" name "\\(") { print type; copying = 1; found = 1 }
        copying { print }
        copying && /^}/ { exit }
        END { if (!found) exit 1 }
    ' "$source" >> "$work/iterator.inc"
}
extract vfs_vnode_iterator_init void
extract vfs_vnode_iterator_destroy void
extract vfs_vnode_iterator_next1 'static struct vnode *'
extract vfs_vnode_iterator_next 'struct vnode *'
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O2 -g -fno-strict-aliasing ${CFLAGS:-} \
    -I"$work" "$dir/test.c" -o "$work/test"
if [ "${ANALYZE:-no}" = yes ]; then
    cppcheck --enable=warning,style,performance,portability --error-exitcode=98 \
        --inline-suppr --suppress=missingIncludeSystem \
        --suppress="variableScope:$work/iterator.inc" -I"$work" "$dir/test.c"

fi
if [ "${MEMCHECK:-no}" = yes ]; then
    valgrind --quiet --leak-check=full --show-leak-kinds=all \
        --errors-for-leak-kinds=all --error-exitcode=99 "$work/test"
else
    "$work/test"
fi
