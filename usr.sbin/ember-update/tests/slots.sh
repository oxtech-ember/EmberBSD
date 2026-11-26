#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Set CC to the selected host C compiler.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cd "$root"
cc=${CC:-cc}
inc="-Icommon/lib/libemberupdate -Iexternal/bsd/ntp/dist/libjsmn -Iusr.sbin/ember-update -DJSMN_PARENT_LINKS"
"$cc" -Wall -Wextra -Werror $inc -Deu_run=mock_run -Deu_save=mock_save -Dlstat=mock_stat -Dunlink=mock_unlink -c usr.sbin/ember-update/slots.c -o "$tmp/slots.o"
"$cc" -Wall -Wextra -Werror $inc common/lib/libemberupdate/json.c external/bsd/ntp/dist/libjsmn/jsmn.c usr.sbin/ember-update/catalog.c usr.sbin/ember-update/local.c usr.sbin/ember-update/tests/slots.c "$tmp/slots.o" -o "$tmp/slots"
"$tmp/slots"
