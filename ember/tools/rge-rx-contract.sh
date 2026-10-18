#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: actual rge allocation/RX source with native-service boundaries, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/rge-rx.XXXXXXXX")
trap 'rm -rf "$work"' 0 HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
ruby - "${RGE_RX_SOURCE:-$src/sys/dev/pci/if_rge.c}" "$src/sys/dev/pci/if_rgereg.h" "$work" <<'RUBY'
s=File.read(ARGV[0]); h=File.read(ARGV[1]); out=ARGV[2]
# KNF function bodies end with a brace at column zero. No copied algorithm.
names=%w[rge_frame_limit rge_rx_buffer_size rge_jumbo_capabilities MCLGETL rge_allocmem rge_deallocmem rge_load_rxbuf rge_newbuf rge_rx_list_init rge_rx_list_fini rge_tx_list_init rge_rxeof rge_init rge_ioctl]
parts=names.map do |name|
    m=s.match(/^static [^\n]+\n#{Regexp.escape(name)}\(/)
    next if !m && %w[rge_frame_limit rge_rx_buffer_size rge_jumbo_capabilities].include?(name)
    abort "missing actual function #{name}" unless m
    last=s.index("\n}\n",m.end(0)); abort name unless last
    s[m.begin(0)..last+2]
end
File.write(out+'/rge-source.inc',parts.compact.join("\n"))
# Include the actual descriptors, queues, softc, geometry and register macros.
last=h.index('struct rge_mac_bps {'); abort 'missing header boundary' unless last
File.write(out+'/rge-header.inc',h[0...last])
RUBY
case ${RGE_RX_ANALYZE:-0} in 0|1) ;; *) exit 2;; esac
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work" "$src/ember/tools/rge-rx-contract.c" -o "$work/test"
if [ "${RGE_RX_ANALYZE:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
        --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -I"$work" "$src/ember/tools/rge-rx-contract.c" 2> "$work/cppcheck.log"
    cat "$work/cppcheck.log"; test ! -s "$work/cppcheck.log"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror -I"$work" "$@" \
        "$src/ember/tools/rge-rx-contract.c" -o "$work/rge.plist" 2> "$work/csa.log"
    cat "$work/csa.log"; test ! -s "$work/csa.log"
fi
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test" "${RGE_RX_CASE:-all}"
else
    "$work/test" "${RGE_RX_CASE:-all}"
fi
