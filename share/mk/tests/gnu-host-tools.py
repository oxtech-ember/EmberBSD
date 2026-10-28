#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check that GNU build-time tools use host compilers and flags."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('host-prefix', 'host-sysroot', 'work-dir'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
for path in (a.host_prefix, a.host_sysroot, a.work_dir):
    if not path.is_absolute() or not path.is_dir():
        p.error('select existing absolute directories')
root = Path(__file__).resolve().parents[3]
host = a.host_prefix
with tempfile.TemporaryDirectory(prefix='gnu-host-tools-', dir=a.work_dir) as temporary:
    work = Path(temporary)
    source = work / 'source'
    source.mkdir()
    (source / 'host.c').write_text('#ifndef HOST_SELECTED\n#error missing host flags\n#endif\n#ifdef TARGET_SELECTED\n#error target flags reached host\n#endif\nint main(void) { return 0; }\n')
    (source / 'target.c').write_text('#ifndef TARGET_SELECTED\n#error missing target flags\n#endif\n#ifdef HOST_SELECTED\n#error host flags reached target\n#endif\nint target(void) { return 1; }\n')
    (source / 'configure').write_text('''set -eu
mkdir -p tools/gnulib/lib tools/info target
cat > Makefile <<'END'
distclean:
\trm -f Makefile tools/gnulib/lib/Makefile tools/info/Makefile target/Makefile tools/gnulib/lib/native.o tools/info/makedoc target/target.o
END
cat > tools/gnulib/lib/Makefile <<'END'
all: native.o
native.o:
\t$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SOURCE)/host.c -o $@
END
cat > tools/info/Makefile <<'END'
makedoc:
\t$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(SOURCE)/host.c -o $@
END
cat > target/Makefile <<'END'
all: target.o
target.o:
\ttest -x ../tools/info/makedoc
\t$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SOURCE)/target.c -o $@
END
''')
    makefile = work / 'Makefile'
    makefile.write_text('''GNU_SOURCE= %s
GNU_OBJDIR= %s
GNU_BUILD_TRIPLE= aarch64-apple-darwin27.0.0
GNU_HOST_TRIPLE= aarch64--netbsd
HOST_SH= %s
HOST_GMAKE= %s
HOST_CC= %s
HOST_AR= %s
HOST_RANLIB= %s
HOST_LD= %s
HOST_CPPFLAGS= -isysroot %s -DHOST_SELECTED
HOST_CFLAGS= -O1
HOST_LDFLAGS= -isysroot %s --ld-path=%s
CC= %s --target=aarch64--netbsd
CPPFLAGS= -DTARGET_SELECTED
CFLAGS= -O2
GNU_HOST_MAKE_DIRS= tools/gnulib/lib tools/info
GNU_HOST_MAKE_TARGETS.tools/info= makedoc
GNU_HOST_MAKE_ENV= SOURCE=%s
GNU_MAKE_DIRS= target
GNU_MAKE_ENV= SOURCE=%s
.include "%s"
all: gnu-target-build
''' % (source, work / 'build', host / 'bin/mksh', host / 'bin/gmake',
       host / 'bin/clang', host / 'bin/llvm-ar', host / 'bin/llvm-ranlib',
       host / 'bin/ld64.lld', a.host_sysroot, a.host_sysroot, host / 'bin/ld64.lld',
       host / 'bin/clang', source, source, root / 'share/mk/bsd.gnutarget.mk'))
    env = dict(os.environ, PATH=str(host / 'bin') + ':' + str(host / 'gnu/bin'), TMPDIR=str(a.work_dir))
    command = [str(host / 'bin/bmake'), '-r', '-f', str(makefile), 'all']
    env['MAKEOVERRIDES'] = 'CC=/unselected/compiler'
    dry = subprocess.run([command[0], '-n', *command[1:]], cwd=work, env=env,
                         capture_output=True, text=True)
    if dry.returncode or (work / 'build').exists():
        raise SystemExit('dry run executed host GNU producer')
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    native = work / 'build/tools/info/makedoc'
    target = work / 'build/target/target.o'
    result = subprocess.run([str(native)], env=env)
    if result.returncode:
        raise SystemExit('native GNU build tool failed to run')
    info = subprocess.run([str(host / 'bin/llvm-readelf'), '-h', str(target)],
                          env=env, check=True, capture_output=True, text=True)
    if 'AArch64' not in info.stdout:
        raise SystemExit('target GNU build used the wrong compiler')
    first = (native.stat().st_ino, target.stat().st_ino)
    subprocess.run(command, cwd=work, env=env, check=True, capture_output=True, text=True)
    if (native.stat().st_ino, target.stat().st_ino) != first:
        raise SystemExit('unchanged host and target outputs were not reused')
    makefile.write_text(makefile.read_text().replace('-DHOST_SELECTED', '-DHOST_SELECTED -DHOST_VERSION=2'))
    subprocess.run(command, cwd=work, env=env, check=True, capture_output=True, text=True)
    if native.stat().st_ino == first[0] or target.stat().st_ino == first[1]:
        raise SystemExit('changed host build flags left stale GNU objects')
print('PASS: selected host GNU tools and target compilation')
