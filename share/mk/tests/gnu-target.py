#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check selected tools and incremental GNU target builds without execution."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('host-prefix', 'sysroot', 'work-dir'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
for path in (a.host_prefix, a.sysroot, a.work_dir):
    if not path.is_absolute() or not path.is_dir():
        p.error('select existing absolute input directories')
root = Path(__file__).resolve().parents[3]
host = a.host_prefix
with tempfile.TemporaryDirectory(prefix='gnu-target-', dir=a.work_dir) as temporary:
    work = Path(temporary)
    source = work / 'source'
    source.mkdir()
    (source / 'fixture.c').write_text('int target_value(void) { return VALUE; }\n')
    (source / 'configure').write_text('''set -eu
printf '%s\\n' "$@" > configured.args
printf '%s\\n' "$CC" "$CPPFLAGS" "$LDFLAGS" "$CFLAGS" > selected.env
cat > Makefile <<'END'
all: fixture.o
distclean:
\trm -f fixture.o Makefile
fixture.o:
\t$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SOURCE)/fixture.c -o $@
END
''')
    makefile = work / 'Makefile'
    makefile.write_text('''GNU_SOURCE= %s
GNU_OBJDIR= %s
GNU_BUILD_TRIPLE= aarch64-apple-darwin27.0.0
GNU_HOST_TRIPLE= aarch64--netbsd
HOST_SH= %s
HOST_GMAKE= %s
CC= %s --target=aarch64--netbsd
CPPFLAGS= --sysroot=%s
CFLAGS= -O1 -Werror -Wstrict-prototypes -DVALUE=1
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}
LDFLAGS= --sysroot=%s
LD_DRIVER_FLAGS= --ld-path=%s
GNU_MAKE_ENV= SOURCE=%s
.include "%s"
all: gnu-target-build
''' % (source, work / 'build', host / 'bin/mksh', host / 'bin/gmake',
       host / 'bin/clang', a.sysroot, a.sysroot, host / 'bin/ld.lld', source,
       root / 'share/mk/bsd.gnutarget.mk'))
    env = dict(os.environ, PATH=str(host / 'bin') + ':' + str(host / 'gnu/bin'),
               TMPDIR=str(a.work_dir))
    command = [str(host / 'bin/bmake'), '-r', '-f', str(makefile), 'all']
    dry = subprocess.run([str(host / 'bin/bmake'), '-n', '-r', '-f', str(makefile), 'all'],
                         cwd=work, env=env, capture_output=True, text=True)
    if dry.returncode or (work / 'build').exists():
        raise SystemExit('dry run created a new GNU object directory')
    previous = None
    for _ in range(2):
        result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(result.stderr + result.stdout)
        current = ((work / 'build/Makefile').stat().st_mtime_ns,
                   (work / 'build/fixture.o').stat().st_mtime_ns)
        if previous is not None and current != previous:
            raise SystemExit('unchanged GNU build did not reuse its configured objects')
        previous = current
    build = work / 'build'
    args = (build / 'configured.args').read_text().splitlines()
    if '--build=aarch64-apple-darwin27.0.0' not in args or '--host=aarch64--netbsd' not in args:
        raise SystemExit('configure did not receive the explicit host and target triples')
    selected = (build / 'selected.env').read_text()
    if str(host / 'bin/clang') not in selected or '--sysroot=' + str(a.sysroot) not in selected:
        raise SystemExit('configure did not receive the selected target compiler and sysroot')
    if '-Werror' in selected:
        raise SystemExit('configure probes received fatal warning policy')
    result = subprocess.run([str(host / 'bin/llvm-readelf'), '-h', str(build / 'fixture.o')],
                            env=env, capture_output=True, text=True)
    if result.returncode or 'AArch64' not in result.stdout:
        raise SystemExit('GNU producer did not compile an AArch64 target object')
    (build / 'fixture.o').unlink()
    (source / 'fixture.c').write_text('int target_value() { return 7; }\n')
    warning = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if warning.returncode == 0 or 'error:' not in warning.stderr:
        raise SystemExit('actual build failed to enforce -Werror')
    (source / 'fixture.c').write_text('int target_value(void) { return VALUE; }\n')
    result = subprocess.run([str(host / 'bin/bmake'), '-n', '-r', '-f', str(makefile), 'all'],
                            cwd=work, env=env, capture_output=True, text=True)
    if result.returncode or (build / 'fixture.o').exists():
        raise SystemExit('dry run executed the GNU target build')
    subprocess.run(command, cwd=work, env=env, check=True, capture_output=True, text=True)
    # Changed build flags must rebuild the object, not just configure.
    first_object = (build / 'fixture.o').read_bytes()
    old_inputs = (build / '.configure-inputs').read_bytes()
    generated = build / 'Makefile'
    good_makefile = generated.read_text()
    generated.write_text(good_makefile.replace('\trm -f fixture.o Makefile',
        '\tif test -f failonce; then rm failonce Makefile; exit 1; fi; rm -f fixture.o Makefile'))
    (build / 'failonce').write_text('1')
    makefile.write_text(makefile.read_text().replace('-DVALUE=1', '-DVALUE=2'))
    failed_clean = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if failed_clean.returncode == 0 or (build / '.configure-inputs').read_bytes() != old_inputs:
        raise SystemExit('failed GNU clean committed changed inputs')
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode or (build / 'fixture.o').read_bytes() == first_object:
        raise SystemExit('changed build flags left a stale target object')
    # Reverting inputs after partial clean must still finish that clean.
    before_inode = (build / 'fixture.o').stat().st_ino
    generated.write_text(generated.read_text().replace('\trm -f fixture.o Makefile',
        '\tif test -f failonce; then rm failonce Makefile; exit 1; fi; rm -f fixture.o Makefile'))
    (build / 'failonce').write_text('1')
    makefile.write_text(makefile.read_text().replace('-DVALUE=2', '-DVALUE=3'))
    if subprocess.run(command, cwd=work, env=env, capture_output=True).returncode == 0:
        raise SystemExit('partial GNU clean did not fail')
    makefile.write_text(makefile.read_text().replace('-DVALUE=3', '-DVALUE=2'))
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode or (build / 'fixture.o').stat().st_ino == before_inode:
        raise SystemExit('reverted inputs reused an object after partial clean')
    # A changed configure argument must take effect even on the next invocation.
    makefile.write_text(makefile.read_text().replace('GNU_MAKE_ENV=',
        'GNU_CONFIGURE_ARGS= --prefix=/changed\nGNU_MAKE_ENV='))
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode or '--prefix=/changed' not in (build / 'configured.args').read_text():
        raise SystemExit('changed configure argument was silently ignored')
    makefile.write_text(makefile.read_text().replace('CPPFLAGS= --sysroot=',
        'CPPFLAGS= -DEMBER_CHANGED=1 --sysroot='))
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode or '-DEMBER_CHANGED=1' not in (build / 'selected.env').read_text():
        raise SystemExit('changed target compiler flags were silently ignored')
    failed_source = work / 'failed-source'
    failed_source.mkdir()
    (failed_source / 'configure').write_text('echo partial > Makefile\nexit 1\n')
    failed_makefile = work / 'failed.mk'
    failed_makefile.write_text(makefile.read_text().replace(str(source), str(failed_source))
                               .replace(str(work / 'build'), str(work / 'failed-build')))
    result = subprocess.run([str(host / 'bin/bmake'), '-r', '-f', str(failed_makefile), 'all'],
                            cwd=work, env=env, capture_output=True, text=True)
    if result.returncode == 0 or (work / 'failed-build/Makefile').exists():
        raise SystemExit('failed configure left a reusable partial Makefile')
print('PASS: selected configure/make, incremental GNU target compile')
