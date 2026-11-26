#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# The release may be archived. The test checks expiry at its signed boundaries.
set -eu
if [ "$#" -ne 3 ]; then
	echo 'usage: signed.sh ROOT.pem current.p7m release.p7m' >&2
	exit 2
fi
anchor=$(realpath "$1")
channel=$(realpath "$2")
release=$(realpath "$3")
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cd "$root"
cc=${CC:-cc}
inc="-Icommon/lib/libemberupdate -Iexternal/bsd/ntp/dist/libjsmn -Iusr.sbin/ember-update -DJSMN_PARENT_LINKS"
"$cc" -Wall -Wextra -Werror ${CPPFLAGS:-} $inc common/lib/libemberupdate/json.c \
    external/bsd/ntp/dist/libjsmn/jsmn.c usr.sbin/ember-update/catalog.c \
    usr.sbin/ember-update/verify.c usr.sbin/ember-update/tests/signed.c \
    ${LDFLAGS:-} -lcrypto -o "$tmp/signed"
"$tmp/signed" "$anchor" "$channel" "$release"
