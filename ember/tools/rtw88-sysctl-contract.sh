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
python3 - "$source" "$src/sys/sys/sysctl.h" "$src/sys/kern/kern_sysctl.c" "$src/sbin/sysctl/sysctl.c" "$tools/rtw88-sysctl-contract.c" "$work" <<'PY'
from pathlib import Path
import sys,re
source,header,kernel,cli,fixture,out=map(Path,sys.argv[1:])
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
# Keep the real generic lookup and CLI string retry, including their errors.
for line in h.splitlines(keepends=True):
 if re.match(r'^#define\s+(?:CTLTYPE_(?:NODE|INT|STRING|QUAD|STRUCT|BOOL)|CTLFLAG_\w+|SYSCTL_(?:VERS_MASK|VERS_0|VERS_1|VERSION|VERS\(|TYPEMASK|TYPE\())',line):parts.append(line)
(out/'native-sysctl-signature.inc').write_text(''.join(parts))
k=kernel.read_text();c=cli.read_text()
lookup=re.search(r'^sysctl_lookup\(.*?^}',k,re.M|re.S).group()
(out/'native-sysctl-lookup.inc').write_text(k[:k.index('#include')]+'static int\n'+lookup+'\n')
a=c.index('\tcase CTLTYPE_STRING: {\n\t\tunsigned char buf[1024], *tbuf;')
b=c.index('\n\tcase CTLTYPE_QUAD:',a)
(out/'native-sysctl-cli.inc').write_text(c[:c.index('#include')]+'static void\nnative_cli_string(void) {\nint name[1]={0},rc;unsigned namelen=0;size_t sz;\nstruct sysctlnode root={.sysctl_flags=SYSCTL_VERSION|CTLTYPE_STRING,.marker=0x12345678};\nconst struct sysctlnode *pnode=&root;const char *gsname="stats";\nswitch(CTLTYPE_STRING) {\n'+c[a:b]+'\n}\n}\n')
size=re.search(r'\tif \(error == 0 && SCARG\(uap, oldv\) != NULL && savelen < oldlen\)\n\t\terror = ENOMEM;',k).group()
(out/'native-sysctl-size.inc').write_text('static int\nnative_size_status(int error,size_t savelen,void *oldv,size_t oldlen) {\nstruct { void *oldv; } args={oldv},*uap=&args;\n#define SCARG(p,f) ((p)->f)\n'+size+'\n#undef SCARG\nreturn error;\n}\n')

# Compile both controls through the exact visible host service. A real
# uninitialized byte must remain diagnosed; no generic/CLI body is changed.
copy=re.search(r'^static int sysctl_copyout\(.*?^}',fixture.read_text(),re.M|re.S).group()
for name,init in [('initialized','={0x31}'),('uninitialized','')]:
    (out/('copy-'+name+'.c')).write_text('#include <stddef.h>\nstruct lwp { int unused; };\nstatic int copy_error;\n'+copy+'\nint main(void) { unsigned char src[1]'+init+', dst[1]={0}; int e=sysctl_copyout(NULL,src,dst,1); return e?e:dst[0]!=0x31; }\n')

PY
# Keep the frame ceiling with the selected compiler family.
stack_warning=-Wstack-usage=3584
if ! printf 'void valid(void) {}\n' | ${CC:-cc} -std=c11 -Werror "$stack_warning" \
    -x c -c -o "$work/stack-probe.o" - > "$work/stack-probe.log" 2>&1; then
    stack_warning=-Wframe-larger-than=3584
    printf 'void valid(void) {}\n' | ${CC:-cc} -std=c11 -Werror "$stack_warning" \
        -x c -c -o "$work/stack-probe.o" -
fi
cat > "$work/oversized.c" <<'STACKCONTROL'
extern void consume(void *);
void oversized(void) { char bytes[8192]; consume(bytes); }
STACKCONTROL
if ${CC:-cc} -std=c11 -Wall -Wextra -Werror "$stack_warning" \
    -c "$work/oversized.c" -o "$work/oversized.o" > "$work/stack-control.log" 2>&1; then
    echo 'Stack budget control unexpectedly compiled' >&2
    exit 1
fi
python3 - "$work/stack-control.log" <<'STACKCHECK'
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
assert 'stack-usage' in text or 'frame-larger-than' in text
print('Causal 3584-byte stack budget: oversized frame rejected')
STACKCHECK
${CC:-cc} -std=c11 -Wall -Wextra -Werror -fno-inline -fstack-usage "$stack_warning" \
    ${RTW88_TEST_CFLAGS:-} -I"$work" "$tools/rtw88-sysctl-contract.c" -o "$work/test"
for usage in "$work"/*.su; do
    awk '/rtwce_sysctl_stats/ { print "Wrapper stack usage: " $0 }' "$usage"
done
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
if [ "${RTW88_PCI_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 -O2 -g -Wall -Wextra -Werror -fanalyzer \
        -fno-inline "$work/copy-initialized.c" -o "$work/copy-initialized"
    if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/copy-initialized"; else "$work/copy-initialized"; fi
    if ${CC:-cc} -std=c11 -O2 -g -Wall -Wextra -Werror -fanalyzer \
        -fno-inline -c "$work/copy-uninitialized.c" -o "$work/copy-uninitialized.o" \
        > "$work/copy-uninitialized.log" 2>&1; then
        echo "Uninitialized-copy analyzer control unexpectedly compiled" >&2
        exit 1
    fi
    cat "$work/copy-uninitialized.log"
    python3 - "$work/copy-uninitialized.log" <<'PYCONTROL'
from pathlib import Path
import sys
text=Path(sys.argv[1]).read_text()
assert 'error: use of uninitialized value' in text
assert '[-Werror=analyzer-use-of-uninitialized-value]' in text
print('Actual copy-service control: initialized PASS; uninitialized O2 analyzer rejection PASS.')
PYCONTROL
    ${CC:-cc} -std=c11 -O0 -g -Wall -Wextra -Werror -fanalyzer \
        -fno-inline "$stack_warning" -I"$work" \
        -c "$tools/rtw88-sysctl-contract.c" -o "$work/analysis.o"
    ${CC:-cc} -std=c11 -dM -E - < /dev/null > "$work/predefines.h"
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --check-level=exhaustive --include="$work/predefines.h" -I"$work" \
        "$tools/rtw88-sysctl-contract.c"
    status=0
    ${CLANG:-clang} ${RTW88_CLANG_FLAGS:-} --analyze -std=c11 \
        -Wall -Wextra -Werror -Xanalyzer -analyzer-output=text \
        -Xanalyzer -analyzer-werror -I"$work" \
        "$tools/rtw88-sysctl-contract.c" -o "$work/sysctl.plist" \
        > "$work/csa.log" 2>&1 || status=$?
    cat "$work/csa.log"
    if [ "$status" -ne 0 ]; then
        # Keep the complete generic read/write body. Its unchanged initial
        # error assignment has one known CSA dead-store diagnostic. Match its
        # exact source hash and location; any other diagnostic remains fatal.
        python3 - "$work" "$status" <<'PYCSA'
from pathlib import Path
import hashlib,re,sys
work=Path(sys.argv[1]);assert int(sys.argv[2])==1
text=(work/'native-sysctl-lookup.inc').read_text()
body=re.search(r'^sysctl_lookup\(.*?^}',text,re.M|re.S).group()
assert hashlib.sha256(body.encode()).hexdigest()=='35caa66b6c63dc5bdc0bfd8c2a539fab0fd85e9be12321ca1fc0093b85ec9e64'
report=(work/'csa.log').read_text()
errors=[line for line in report.splitlines() if ': error:' in line]
assert len(errors)==1,errors
m=re.fullmatch(r'(.*/native-sysctl-lookup\.inc):(\d+):\d+: error: Value stored to \'error\' is never read \[deadcode\.DeadStores\]',errors[0])
assert m,errors
assert Path(m[1])==work/'native-sysctl-lookup.inc'
assert text.splitlines()[int(m[2])-1].strip()=='error = 0;'
assert report.rstrip().endswith('1 error generated.')
print('CSA: 1 pinned generic lookup baseline finding; 0 new findings. Not clean.')
PYCSA
    fi
fi
