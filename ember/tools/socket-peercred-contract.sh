#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; execute the LOCAL_PEEREID socketpair regression.
set -eu
[ "$(uname -s)" = NetBSD ] || { echo 'Run on EmberBSD/NetBSD' >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/socket-peercred.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -D_NETBSD_SOURCE -Wall -Wextra -Werror \
    "$tools/socket-peercred-contract.c" -o "$work/test"
"$work/test"
