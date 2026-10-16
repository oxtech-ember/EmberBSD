#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors
# Original, AI-assisted software contract. No firmware is downloaded.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
build=${BUILD_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/cixvpu-fw.XXXXXXXX")}
mkdir -p "$build"
printf 'Build directory: %s\n' "$build"
printf '%s\n' 'Build products remain here for caller inspection and cleanup.'
${CC:-cc} --version
# CC, CPPFLAGS, CFLAGS, LDFLAGS, and TEST_RUNNER use normal shell word lists.
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CPPFLAGS:-} ${CFLAGS:--O2 -g} \
    "$src/ember/tools/cixvpu-fw-test.c" "$src/sys/dev/ic/cixvpu_fw.c" \
    ${LDFLAGS:-} -o "$build/cixvpu-fw-test"
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${CPPFLAGS:-} ${CFLAGS:--O2 -g} \
    "$src/ember/tools/cixvpu-fw-check.c" "$src/sys/dev/ic/cixvpu_fw.c" \
    ${LDFLAGS:-} -o "$build/cixvpu-fw-check"
${TEST_RUNNER:-} "$build/cixvpu-fw-test" "$build/synthetic.fwb"
${TEST_RUNNER:-} "$build/cixvpu-fw-check" "$build/synthetic.fwb" \
    > "$build/synthetic-metadata.txt"
grep -Fx 'protocol=3.5' "$build/synthetic-metadata.txt"
grep -Fx 'format-bytes=8,8' "$build/synthetic-metadata.txt"
grep -Fx 'shared-pages=2' "$build/synthetic-metadata.txt"
grep -Fx 'private-pages=3' "$build/synthetic-metadata.txt"
grep -F 'description=\x1b\x5c\x0aAAA' "$build/synthetic-metadata.txt"
expect_refusal()
{
    expected=$1
    label=$2
    shift 2
    status=0
    ${TEST_RUNNER:-} "$build/cixvpu-fw-check" "$@" \
        > "$build/$label.stdout" 2> "$build/$label.stderr" || status=$?
    if [ "$status" -ne "$expected" ]; then
        printf 'FAIL: %s returned %s; expected %s\n' \
            "$label" "$status" "$expected" >&2
        exit 1
    fi
}
expect_refusal 1 missing-file "$build/absent.fwb"
expect_refusal 2 usage
: > "$build/empty.fwb"
expect_refusal 1 short "$build/empty.fwb"
grep -F 'firmware header is truncated' "$build/short.stderr"
dd if="$build/synthetic.fwb" of="$build/truncated-text.fwb" \
    bs=180 count=1 2> "$build/truncate.log"
expect_refusal 1 truncated-text "$build/truncated-text.fwb"
grep -F 'invalid firmware text extent' "$build/truncated-text.stderr"
expect_refusal 1 directory "$build"
grep -F 'requires a regular, addressable file' "$build/directory.stderr"
printf '%s\n' 'PASS: firmware CLI success and refusal controls'
