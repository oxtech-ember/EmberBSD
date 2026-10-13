#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; execute the socket per-call nonblocking regression.
set -eu
[ "$(uname -s)" = NetBSD ] || { echo 'Run on EmberBSD/NetBSD' >&2; exit 2; }
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/socket-nonblock.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -D_NETBSD_SOURCE -Wall -Wextra -Werror \
    "$tools/socket-nonblock-contract.c" -o "$work/test"
"$work/test"
