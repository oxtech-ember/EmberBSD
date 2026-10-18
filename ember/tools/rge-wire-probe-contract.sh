#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/rge-wire.XXXXXXXX")
trap 'rm -rf "$work"' 0 HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
ruby - "$src" "$work" <<'RUBY'
src, work = ARGV
h = File.read(src + '/sys/net/bpf.h')
pieces = ['struct bpf_timeval', 'struct bpf_hdr'].map do |name|
  h[/^#{name} \{.*?^\};/m] || abort('missing native ' + name)
end
pieces << h[/^#define BPF_CLASS.*?(?=^\/\*\n \* Auxiliary data)/m]
pieces << h[/^typedef struct bpf_args \{.*?^\} bpf_args_t;/m]
pieces << h.lines.grep(/^#define\s+BPF_MEMWORDS\b/).join
f = File.read(src + '/sys/net/bpf_filter.c')
body = f[/^__strong_alias\(pcapint_filter, bpf_filter\).*?(?=^\/\*\n \* Return true)/m] || abort('missing native interpreter')
body = body.sub(/^__strong_alias.*\n/, '')
File.write(work + '/wire-filter.inc', f[0...f.index('#include')] +
  "#define EXTRACT_SHORT(p) (((uint32_t)(p)[0] << 8) | (p)[1])\n" +
  "#define EXTRACT_LONG(p) (((uint32_t)(p)[0] << 24) | ((uint32_t)(p)[1] << 16) | ((uint32_t)(p)[2] << 8) | (p)[3])\n" +
  "#include <stdlib.h>\n" + body.sub("#endif\n{", "{"))
pieces += h.lines.grep(/^#define BPF_(?:ALIGNMENT|WORDALIGN)\b/)
File.write(work + '/wire-bpf.h', pieces.join("\n") + "\n")
RUBY
"${CC:-cc}" -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -O2 -g -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work" "$src/ember/tools/rge-wire-probe-contract.c" -o "$work/test"
case ${RGE_WIRE_ANALYZE:-0} in 0|1) ;; *) exit 2;; esac
if [ "${RGE_WIRE_ANALYZE:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    if ! cppcheck --quiet --check-level=exhaustive \
        --enable=warning,performance,portability --error-exitcode=1 \
        --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -DRGE_WIRE_TEST=1 -I"$work" \
        "$src/ember/tools/rge-wire-probe-contract.c" 2> "$work/cppcheck.log"; then
        cat "$work/cppcheck.log" >&2; exit 1
    fi
    cat "$work/cppcheck.log"; test ! -s "$work/cppcheck.log"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror \
        -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -I"$work" "$@" \
        "$src/ember/tools/rge-wire-probe-contract.c" -o "$work/analysis.plist" \
        2> "$work/csa.log"; then
        cat "$work/csa.log" >&2; exit 1
    fi
    cat "$work/csa.log"; test ! -s "$work/csa.log"
fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
