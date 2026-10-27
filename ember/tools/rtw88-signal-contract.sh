#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 0 ] || exit 2
case ${RTW88_START_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-signal-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then SDKROOT=$(xcrun --show-sdk-path);export SDKROOT;fi
run_test() { if [ -n "${TEST_RUNNER:-}" ];then "$TEST_RUNNER" "$@";else "$@";fi; }
mkdir -p "$work/include/sys" "$work/include/dev" "$work/include/net80211"
ln -s "$src/sys/dev/ic" "$work/include/dev/ic"
ln -s "$src/sys/net80211/ieee80211_phy_ies.h" "$work/include/net80211/ieee80211_phy_ies.h"
for h in param systm;do printf '#include <errno.h>\n#include "rtw88-test-kernel.h"\n' > "$work/include/sys/$h.h";done
python3 "$tools/rtw88-signal-contract.py" "$src" "$work"
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
 -I"$src/sys/dev/ic" "$tools/rtw88-signal-contract.c" "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8822c_hw.c" -o "$work/parser"
run_test "$work/parser"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${RTW88_TEST_CFLAGS:-} \
    -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" -I"$src/sys/net80211" \
    "$tools/rtw88-signal-core-contract.c" \
    "$tools/rtw88-signal-scan-contract.c" \
    "$src/sys/dev/ic/rtw88.c" \
    "$src/sys/dev/ic/rtw88_chip.c" \
    "$src/sys/dev/ic/rtw8822c_runtime.c" \
    "$src/sys/dev/pci/rtw88_pci_hci.c" \
    "$src/sys/dev/pci/rtw88_pci_packet.c" \
    "$src/sys/dev/ic/rtw88_net_policy.c" \
    "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8822c_hw.c" \
    "$src/sys/dev/ic/rtw88_fw_init.c" \
    "$src/sys/dev/ic/rtw8822c.c" \
    "$src/sys/dev/ic/rtw8822c_boot.c" \
    "$src/sys/dev/ic/rtw8822c_power.c" \
    "$src/sys/dev/ic/rtw8822c_mac.c" \
    "$src/sys/dev/ic/rtw8822c_phy.c" \
    "$src/sys/dev/ic/rtw8822c_dack.c" \
    "$src/sys/dev/ic/rtw8822c_table.c" \
    "$src/sys/dev/ic/rtw8822c_coex.c" \
    "$src/sys/dev/ic/rtw8822c_txpwr.c" \
    -o "$work/test"
RTW88_SIGNAL_CONTEXT="$work/context" run_test "$work/test"
cp "$src/sys/net80211/_ieee80211.h" "$work/include/net80211/"
python3 "$tools/rtw88-fixed-contract.py" "$src" "$work/include"
python3 "$tools/net80211-wme-contract.py" "$src" "$work/include"
python3 "$tools/rtw88-routing-contract.py" "$src" "$work/include"
${CC:-cc} -std=c11 -Wall -Werror ${RTW88_TEST_CFLAGS:-} \
    -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" -I"$src/sys/net80211" \
    "$tools/rtw88-signal-queue-contract.c" \
    "$tools/rtw88-signal-scan-contract.c" \
    "$src/sys/dev/ic/rtw88.c" \
    "$src/sys/dev/ic/rtw88_chip.c" \
    "$src/sys/dev/ic/rtw8822c_runtime.c" \
    "$src/sys/dev/pci/rtw88_pci_hci.c" \
    "$src/sys/dev/pci/rtw88_pci_packet.c" \
    "$src/sys/dev/ic/rtw88_net_policy.c" \
    "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8822c_hw.c" \
    "$src/sys/dev/ic/rtw88_fw_init.c" \
    "$src/sys/dev/ic/rtw8822c.c" \
    "$src/sys/dev/ic/rtw8822c_boot.c" \
    "$src/sys/dev/ic/rtw8822c_power.c" \
    "$src/sys/dev/ic/rtw8822c_mac.c" \
    "$src/sys/dev/ic/rtw8822c_phy.c" \
    "$src/sys/dev/ic/rtw8822c_dack.c" \
    "$src/sys/dev/ic/rtw8822c_table.c" \
    "$src/sys/dev/ic/rtw8822c_coex.c" \
    "$src/sys/dev/ic/rtw8822c_txpwr.c" \
    -o "$work/queue"
case ${RTW88_CONTEXT_ONLY:-} in
matrix|failure|power-validation)
    ;;
*)
    RTW88_SIGNAL_CONTEXT="$work/context" run_test "$work/queue"
    ;;
esac
if [ "${RTW88_START_ANALYZE:-0}" = 1 ]; then
 printf 'void causal_null(void){int *p=0;*p=1;}\n' > "$work/causal.c"
 if ${CC:-cc} -std=c11 -O0 -Wall -Werror -fanalyzer -c "$work/causal.c" -o "$work/causal.o" > "$work/causal.log" 2>&1;then exit 1;fi
 grep -F analyzer-null-dereference "$work/causal.log" >/dev/null
 echo 'GCC analyzer causal NULL: rejected'
 for unit in "$tools/rtw88-signal-contract.c" "$tools/rtw88-signal-core-contract.c" "$tools/rtw88-signal-queue-contract.c" "$tools/rtw88-signal-scan-contract.c" "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8822c_hw.c" "$src/sys/dev/ic/rtw8822c_phy.c" "$src/sys/dev/ic/rtw88.c" "$src/sys/dev/ic/rtw88_chip.c" "$src/sys/dev/ic/rtw8822c_runtime.c" "$src/sys/dev/pci/rtw88_pci_hci.c" "$src/sys/dev/pci/rtw88_pci_packet.c";do
  ${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -O0 -g -Wall -Werror -fanalyzer \
   -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" -I"$src/sys/net80211" -c "$unit" -o "$work/analysis.o"
  ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE \
   -Wall -Werror -Xanalyzer -analyzer-output=text -Xanalyzer -analyzer-werror \
   -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" -I"$src/sys/net80211" "$unit" -o "$work/analysis.plist"
 done
 ${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -include sys/queue.h -dM -E - < /dev/null > "$work/predefines.h"
 cppcheck --enable=warning,performance,portability --error-exitcode=1 --check-level=exhaustive \
  --include="$work/predefines.h" -I"$work" -I"$work/include" -I"$tools" -I"$src/sys/dev/ic" -I"$src/sys/net80211" \
  "$tools/rtw88-signal-contract.c" "$tools/rtw88-signal-core-contract.c" "$tools/rtw88-signal-queue-contract.c" \
  "$tools/rtw88-signal-scan-contract.c" "$src/sys/dev/ic/rtw88_hw.c" "$src/sys/dev/ic/rtw8822c_hw.c" "$src/sys/dev/ic/rtw8822c_phy.c" "$src/sys/dev/ic/rtw88.c" "$src/sys/dev/pci/rtw88_pci_hci.c" "$src/sys/dev/pci/rtw88_pci_packet.c"
fi
