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
names=%w[rge_jumbo_free rge_jumbo_mbuf rge_unload_rxbuf rge_frame_limit rge_rx_buffer_size rge_jumbo_capabilities MCLGETL rge_allocmem rge_deallocmem rge_load_rxbuf rge_newbuf rge_rx_list_init rge_rx_list_fini rge_tx_list_init rge_rxeof rge_init rge_ioctl]
parts=names.map do |name|
    m=s.match(/^static [^\n]+\n#{Regexp.escape(name)}\(/)
    next if !m && %w[rge_jumbo_free rge_jumbo_mbuf rge_unload_rxbuf rge_frame_limit rge_rx_buffer_size rge_jumbo_capabilities].include?(name)
    abort "missing actual function #{name}" unless m
    last=s.index("\n}\n",m.end(0)); abort name unless last
    s[m.begin(0)..last+2]
end
owner=s[/^struct rge_jumbo_mem \{.*?^\};/m] || ''
File.write(out+'/rge-source.inc',owner+"\n"+parts.compact.join("\n"))
# Source overrides may point outside the tree; use the canonical source root.
root=File.expand_path('../../../..',ARGV[1])
mbuf=File.read(File.join(root,'sys/sys/mbuf.h'))
macros=%w[MCLINITREFERENCE MEXTADD].map do |name|
  mbuf[/^#define #{name}\(.*?^} while \(0\)/m] or abort "missing #{name}"
end
File.write(out+'/rge-mbuf.inc',macros.join("\n")+"\n")
body=File.read(File.join(root,'sys/kern/uipc_mbuf.c'))
start=body.index("struct mbuf *\nm_devget(") or abort 'missing native m_devget'
last=body.index("\n}\n",start) or abort 'm_devget boundary'
File.write(out+'/rge-devget.inc',"static "+body[start..last+2])
# Include the actual descriptors, queues, softc, geometry and register macros.
last=h.index('struct rge_mac_bps {'); abort 'missing header boundary' unless last
File.write(out+'/rge-header.inc',h[0...last])
RUBY
if [ -n "${RGE_RX_EVIDENCE:-}" ]; then
    mkdir -p "$RGE_RX_EVIDENCE"
    cp "$work"/*.inc "$RGE_RX_EVIDENCE/"
fi
case ${RGE_RX_ANALYZE:-0} in 0|1) ;; *) exit 2;; esac
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work" "$src/ember/tools/rge-rx-contract.c" -o "$work/test"
if [ "${RGE_RX_ANALYZE:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    if ! cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
        --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -I"$work" "$src/ember/tools/rge-rx-contract.c" 2> "$work/cppcheck.log"; then
        cat "$work/cppcheck.log" >&2; exit 1
    fi
    cat "$work/cppcheck.log"; test ! -s "$work/cppcheck.log"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror -I"$work" "$@" \
        "$src/ember/tools/rge-rx-contract.c" -o "$work/rge.plist" 2> "$work/csa.log"; then
        cat "$work/csa.log" >&2; exit 1
    fi
    cat "$work/csa.log"; test ! -s "$work/csa.log"
fi
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test" "${RGE_RX_CASE:-all}"
else
    "$work/test" "${RGE_RX_CASE:-all}"
fi
