#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Actual sysctl wrapper with the target stack budget and explicit host services.
set -eu
[ "$#" -eq 0 ] || exit 2
case ${RTW88_PCI_ANALYZE:-0} in 0|1) ;; *) exit 2 ;; esac
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
source=${RTW88_SYSCTL_SOURCE:-"$src/sys/dev/pci/rtw88_pci_sysctl.c"}
work=$(mktemp -d "${TMPDIR:-/tmp}/rtw88-sysctl-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
python3 - "$source" "$src/sys/sys/sysctl.h" "$work" <<'PY'
from pathlib import Path
import sys,re
source,header,out=map(Path,sys.argv[1:])
s=source.read_text();h=header.read_text()
body=re.search(r'^rtwce_sysctl_stats\(.*?^}',s,re.M|re.S).group()
(out/'native-sysctl-stats.inc').write_text(s[:s.index('#include')]+'static int\n'+body+'\n')
parts=re.findall(r'/\*.*?\*/',h[:h.index('#ifndef')],re.S)
parts=[p+'\n' for p in parts];lines=h.splitlines(keepends=True)
for name in ['SYSCTLFN_ARGS','SYSCTLFN_CALL']:
 for i,line in enumerate(lines):
  if re.match(r'^#define\s+'+name+r'(?:\s|\()',line):
   while line.rstrip().endswith('\\'):
    i+=1;line+=lines[i]
   parts.append(line);break
 else:raise ValueError(name)
(out/'native-sysctl-signature.inc').write_text(''.join(parts))
PY
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fno-inline -fstack-usage -Wstack-usage=3584 \
    ${RTW88_TEST_CFLAGS:-} -I"$work" "$tools/rtw88-sysctl-contract.c" -o "$work/test"
for usage in "$work"/*.su; do
    awk '/rtwce_sysctl_stats/ { print "Wrapper stack usage: " $0 }' "$usage"
done
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
if [ "${RTW88_PCI_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer \
        -fno-inline -Wstack-usage=3584 -I"$work" \
        -c "$tools/rtw88-sysctl-contract.c" -o "$work/analysis.o"
    ${CC:-cc} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" -I"$work" \
        "$tools/rtw88-sysctl-contract.c"
    ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 \
        -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror -I"$work" \
        "$tools/rtw88-sysctl-contract.c" -o "$work/sysctl.plist"
fi
