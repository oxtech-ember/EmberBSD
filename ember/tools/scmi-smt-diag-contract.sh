#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted actual-source tests.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/scmi-smt-diag.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
case "${SCMI_SMT_COMPILE_ONLY:-0}" in 0|1) ;; *) exit 2 ;; esac
if [ "${SCMI_SMT_COMPILE_ONLY:-0}" = 1 ] && [ -z "${SCMI_SMT_OUTPUT:-}" ]; then
    echo 'SCMI_SMT_COMPILE_ONLY requires SCMI_SMT_OUTPUT' >&2
    exit 2
fi
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
source=${SCMI_SMT_SOURCE:-"$src/sys/dev/ic/scmi_smt_diag.c"}
if [ -f "$source" ]; then
    set -- "$source"
else
    set -- -DSCMI_SMT_MISSING_SOURCE
fi
# CFLAGS is a caller-supplied compiler argument list.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$src/sys/dev/ic" "$@" "$src/ember/tools/scmi-smt-diag-contract.c" \
    -o "$work/contract"
if [ -n "${SCMI_SMT_OUTPUT:-}" ]; then cp "$work/contract" "$SCMI_SMT_OUTPUT"; fi
[ "${SCMI_SMT_COMPILE_ONLY:-0}" = 0 ] || exit 0
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/contract"
else
    "$work/contract"
fi
