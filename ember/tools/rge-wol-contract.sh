#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: actual rge Wake-on-LAN source with register/PCI/sysctl boundaries, AI-assisted.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/rge-wol.XXXXXXXX")
trap 'rm -rf "$work"' 0 HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
ruby - "${RGE_WOL_SOURCE:-$src/sys/dev/pci/if_rge.c}" "$src/sys/dev/pci/if_rgereg.h" "$src" "$work" <<'RUBY'
s = File.read(ARGV[0]); h = File.read(ARGV[1]); root = ARGV[2]; out = ARGV[3]
# KNF function bodies end with a brace at column zero. No copied algorithm.
names = %w[rge_wol_disable rge_wol_enable rge_wol_power rge_shutdown rge_sysctl_wol
         rge_sysctl_attach]
parts = names.map do |name|
  m = s.match(/^static [^\n]+\n#{Regexp.escape(name)}\(/)
  abort "missing actual function #{name}" unless m
  last = s.index("\n}\n", m.end(0)); abort name unless last
  s[m.begin(0)..last + 2]
end
File.write(out + '/wol-source.inc', parts.join("\n"))
# The wiring that the extracted bodies cannot show: the shutdown hook is
# registered, and detach releases the sysctl nodes.
abort 'shutdown handler is not registered' unless
  s =~ /pmf_device_register1\(self, NULL, NULL, rge_shutdown\)/
d = s[/^static int\nrge_detach\(.*?^}\n/m] or abort 'missing rge_detach'
abort 'detach does not tear down sysctl' unless
  d.include?('sysctl_teardown(&sc->sc_sysctllog)')
a = s[/^static void\nrge_attach\(.*?^}\n/m] or abort 'missing rge_attach'
abort 'attach does not create sysctl nodes' unless
  a.include?('rge_sysctl_attach(sc)')
abort 'attach does not disarm wake' unless a.include?('rge_wol_disable(sc)')
# Native constants and sysctl function-argument macros.
sysctl = File.read(File.join(root, 'sys/sys/sysctl.h'))
reboot = File.read(File.join(root, 'sys/sys/reboot.h'))
pcireg = File.read(File.join(root, 'sys/dev/pci/pcireg.h'))
netif = File.read(File.join(root, 'sys/net/if.h'))
native = []
%w[SYSCTLFN_PROTO SYSCTLFN_ARGS SYSCTLFN_CALL].each do |n|
  native << (sysctl[/^#define\s+#{n}\b.*?(?<!\\)\n/m] or abort "missing #{n}")
end
%w[CTLTYPE_NODE CTLTYPE_INT CTLFLAG_READWRITE CTL_EOL CTL_CREATE CTL_HW].each do |n|
  native << (sysctl[/^#define\s+#{n}\b[^\n]*\n/] or abort "missing #{n}")
end
%w[RB_HALT RB_POWERDOWN].each do |n|
  native << (reboot[/^#define\s+#{n}\b[^\n]*\n/] or abort "missing #{n}")
end
%w[PCI_PMCSR PCI_PMCSR_STATE_D0 PCI_PMCSR_PME_EN PCI_PMCSR_PME_STS PCI_CAP_PWRMGMT].each do |n|
  native << (pcireg[/^#define\s+#{n}\b[^\n]*\n/] or abort "missing #{n}")
end
native << (netif[/^#define\s+IFF_RUNNING\b[^\n]*\n/] or abort 'missing IFF_RUNNING')
File.write(out + '/wol-native.inc', native.join)
# The actual register map, softc and chip types.
last = h.index('struct rge_mac_bps {'); abort 'missing header boundary' unless last
File.write(out + '/rge-header.inc', h[0...last])
RUBY
if [ -n "${RGE_WOL_EVIDENCE:-}" ]; then
    mkdir -p "$RGE_WOL_EVIDENCE"
    cp "$work"/*.inc "$RGE_WOL_EVIDENCE/"
fi
case ${RGE_WOL_ANALYZE:-0} in 0|1) ;; *) exit 2;; esac
"${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror ${CFLAGS:-} \
    -I"$work" "$src/ember/tools/rge-wol-contract.c" -o "$work/test"
if [ "${RGE_WOL_ANALYZE:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    if ! cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
        --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
        -I"$work" "$src/ember/tools/rge-wol-contract.c" 2> "$work/cppcheck.log"; then
        cat "$work/cppcheck.log" >&2; exit 1
    fi
    cat "$work/cppcheck.log"; test ! -s "$work/cppcheck.log"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    if ! "${CSA:-clang}" --analyze -std=c11 -Wall -Wextra -Werror -I"$work" "$@" \
        "$src/ember/tools/rge-wol-contract.c" -o "$work/rge-wol.plist" 2> "$work/csa.log"; then
        cat "$work/csa.log" >&2; exit 1
    fi
    cat "$work/csa.log"; test ! -s "$work/csa.log"
fi
if [ -n "${TEST_RUNNER:-}" ]; then
    "$TEST_RUNNER" "$work/test"
else
    "$work/test"
fi
