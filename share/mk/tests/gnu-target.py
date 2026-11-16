#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check selected tools and incremental GNU target builds without execution."""
import argparse
import os
import shlex
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
    dependency = work / 'dependency.a'
    dependency.write_bytes(b'first archive')
    metadata = work / 'dependency.pc'
    metadata.write_text('target metadata v1\n')
    makefile = work / 'Makefile'
    makefile.write_text('''GNU_SOURCE= %s
GNU_OBJDIR= %s
GNU_BUILD_TRIPLE= aarch64-apple-darwin27.0.0
GNU_HOST_TRIPLE= aarch64--netbsd
HOST_SH= %s
HOST_GMAKE= %s
HOST_PYTHON= %s
CC= %s --target=aarch64--netbsd
CPPFLAGS= --sysroot=%s
CFLAGS= -O1 -Werror -Wstrict-prototypes -DVALUE=1
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}
LDFLAGS= --sysroot=%s
LD_DRIVER_FLAGS= --ld-path=%s
GNU_MAKE_ENV= SOURCE=%s
GNU_CONFIGURE_DEPENDENCY= %s %s
.include "%s"
all: gnu-target-build
''' % (source, work / 'build', host / 'bin/mksh', host / 'bin/gmake', host / 'bin/python3.15',
       host / 'bin/clang', a.sysroot, a.sysroot, host / 'bin/ld.lld', source, dependency, metadata,
       root / 'share/mk/bsd.gnutarget.mk'))
    env = dict(os.environ, PATH=str(host / 'bin') + ':' + str(host / 'gnu/bin'),
               TMPDIR=str(a.work_dir))
    command = [str(host / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'),
               '-f', str(makefile), 'all']
    dry = subprocess.run([str(host / 'bin/bmake'), '-n', '-r', '-m', str(root / 'share/mk'),
                          '-f', str(makefile), 'all'],
                         cwd=work, env=env, capture_output=True, text=True)
    if dry.returncode or (work / 'build').exists():
        raise SystemExit('dry run created a new GNU object directory: rc=%d %s%s' %
                         (dry.returncode, dry.stdout, dry.stderr))
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
    if ('configure-dependency=' + str(dependency) + ' ' + str(metadata)) not in (build / '.configure-inputs').read_text().splitlines():
        raise SystemExit('dependency snapshot lacks its role marker')
    args = (build / 'configured.args').read_text().splitlines()
    if '--build=aarch64-apple-darwin27.0.0' not in args or '--host=aarch64--netbsd' not in args:
        raise SystemExit('configure did not receive the explicit host and target triples')
    selected = (build / 'selected.env').read_text()
    if str(host / 'bin/clang') not in selected or '--sysroot=' + str(a.sysroot) not in selected:
        raise SystemExit('configure did not receive the selected target compiler and sysroot')
    if '--sysroot=' + str(a.sysroot) not in selected.splitlines()[0]:
        raise SystemExit('target compiler driver did not receive the sysroot')
    search = subprocess.check_output(shlex.split(selected.splitlines()[0]) + ['-print-search-dirs'],
                                     env=env, text=True)
    if str(a.sysroot / 'usr/lib') not in search:
        raise SystemExit('target compiler search does not use the target sysroot')
    if 'MacOSX' in search:
        raise SystemExit('target compiler search includes the Darwin host SDK')
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
    result = subprocess.run([str(host / 'bin/bmake'), '-n', '-r', '-m', str(root / 'share/mk'),
                             '-f', str(makefile), 'all'],
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
    # External static dependency content changes invalidate the target graph.
    before_inode = (build / 'fixture.o').stat().st_ino
    metadata.write_text('target metadata v2\n')
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode or (build / 'fixture.o').stat().st_ino == before_inode:
        raise SystemExit('changed second external dependency left a stale target object')
    # Changed configure content must invalidate objects even in the same second.
    before_inode = (build / 'fixture.o').stat().st_ino
    configure = source / 'configure'
    configure.write_text(configure.read_text() + '\n# changed configure input\n')
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode or (build / 'fixture.o').stat().st_ino == before_inode:
        raise SystemExit('changed configure left a stale target object')
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
    # Scoped source refresh must clean objects owned by nested GNU make.
    generated.write_text(generated.read_text().replace('\trm -f fixture.o Makefile',
        '\tif test -f failonce; then rm failonce Makefile; exit 1; fi; rm -f fixture.o Makefile'))
    (build / 'failonce').write_text('1')
    result = subprocess.run(command[:-1] + ['cleandir'], cwd=work, env=env,
                            capture_output=True, text=True)
    if result.returncode == 0:
        raise SystemExit('partial BSD cleandir did not fail')
    result = subprocess.run(command[:-1] + ['cleandir'], cwd=work, env=env,
                            capture_output=True, text=True)
    if result.returncode or (build / 'fixture.o').exists() or build.exists():
        raise SystemExit('BSD cleandir left reusable nested GNU output')
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode or not (build / 'fixture.o').exists():
        raise SystemExit('GNU producer cannot rebuild after BSD cleandir')
    failed_source = work / 'failed-source'
    failed_source.mkdir()
    (failed_source / 'configure').write_text('echo partial > Makefile\nexit 1\n')
    failed_makefile = work / 'failed.mk'
    failed_makefile.write_text(makefile.read_text().replace(str(source), str(failed_source))
                               .replace(str(work / 'build'), str(work / 'failed-build')))
    result = subprocess.run([str(host / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'),
                             '-f', str(failed_makefile), 'all'],
                            cwd=work, env=env, capture_output=True, text=True)
    if result.returncode == 0 or (work / 'failed-build/Makefile').exists():
        raise SystemExit('failed configure left a reusable partial Makefile')

# Exercise the actual OpenSSH source import and the regular GNU configure
# adapter. Prepared WPA and mDNS trees are present in the build environment,
# but they are not inputs to this component and must not invalidate its warm
# configure/object state.
with tempfile.TemporaryDirectory(prefix='gnu-openssh-warm-', dir=a.work_dir) as temporary:
    work = Path(temporary)
    openssh_roots = [work / 'openssh-1', work / 'openssh-2']
    wpa_roots = [work / 'wpa-1', work / 'wpa-2']
    mdns_roots = [work / 'mdns-1', work / 'mdns-2']
    state = work / 'configure-count'
    for index, source in enumerate(openssh_roots):
        source.mkdir()
        (source / 'fixture.c').write_text(
            'int target_value(void) { return %d; }\n' % (index + 1))
        (source / 'configure').write_text('''#!/bin/sh
set -eu
printf 'configured\\n' >> "$STATE"
cat > Makefile <<'END'
all: fixture.o
distclean:
\ttouch distcleaned
\trm -f fixture.o Makefile
fixture.o:
\t$(CC) $(CPPFLAGS) $(CFLAGS) -c $(SOURCE)/fixture.c -o $@
END
''')
        (source / 'configure').chmod(0o755)
    for source in wpa_roots + mdns_roots:
        source.mkdir()

    build_dir = work / 'build'
    build_dir.mkdir()
    gnu_source = build_dir / 'source-view'
    gnu_source.symlink_to(openssh_roots[0], target_is_directory=True)
    makefile = work / 'openssh-config.mk'
    makefile.write_text('''MAKECONF= /dev/null
MACHINE= evbarm
MACHINE_ARCH= aarch64
MKREPRO= yes
EMBER_COMPILER_FAMILY= clang
NETBSDSRCDIR= %s
NETBSDOBJDIR= %s
DESTDIR= %s
EMBER_OPENSSH_SOURCE= %s
EMBER_WPA_SOURCE= %s
EMBER_MDNS_SOURCE= %s
GNU_SOURCE= %s
GNU_OBJDIR= %s
GNU_BUILD_TRIPLE= aarch64-apple-darwin27.0.0
GNU_HOST_TRIPLE= aarch64--netbsd
HOST_SH= %s
HOST_GMAKE= %s
CC= %s --target=aarch64--netbsd
CPPFLAGS= --sysroot=%s
CFLAGS= -O1 -Werror -DVALUE=1
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}
LDFLAGS= --sysroot=%s
LD_DRIVER_FLAGS= --ld-path=%s
GNU_CONFIGURE_ENV= SOURCE=${GNU_SOURCE} STATE=%s
GNU_MAKE_ENV= SOURCE=${GNU_SOURCE}
GNU_MAKE_DIRS= .
.include <bsd.own.mk>
.include "%s"
.include <bsd.sys.mk>
.include <bsd.gnutarget.mk>
all: gnu-target-build
''' % (root, work / 'obj', work / 'dest', openssh_roots[0],
       wpa_roots[0], mdns_roots[0], gnu_source, work / 'build', host / 'bin/mksh',
       host / 'bin/gmake', host / 'bin/clang', a.sysroot, a.sysroot,
       host / 'bin/ld.lld', state,
       root / 'crypto/external/bsd/openssh/source.mk'))
    env = dict(os.environ, PATH=str(host / 'bin') + ':' + str(host / 'gnu/bin'),
               TMPDIR=str(a.work_dir))
    command = [str(host / 'bin/bmake'), '-r', '-m', str(root / 'share/mk'),
               '-f', str(makefile), 'all']
    result = subprocess.run(command, cwd=work, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    build = work / 'build'
    config_inputs = build / '.configure-inputs'
    openssh_map = '-ffile-prefix-map=%s=/usr/src/external/openssh' % openssh_roots[0]
    if openssh_map not in config_inputs.read_text():
        raise SystemExit('actual OpenSSH source import is absent from configure flags')
    before = ((build / 'Makefile').stat().st_ino,
              (build / 'Makefile').stat().st_mtime_ns,
              (build / 'fixture.o').stat().st_ino,
              (build / 'fixture.o').stat().st_mtime_ns)
    before_inputs = config_inputs.read_bytes()
    # Refresh unrelated prepared package roots while the SSH inputs stay fixed.
    result = subprocess.run(command + ['EMBER_WPA_SOURCE=' + str(wpa_roots[1]),
                                       'EMBER_MDNS_SOURCE=' + str(mdns_roots[1])],
                            cwd=work, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    after = ((build / 'Makefile').stat().st_ino,
             (build / 'Makefile').stat().st_mtime_ns,
             (build / 'fixture.o').stat().st_ino,
             (build / 'fixture.o').stat().st_mtime_ns)
    after_inputs = config_inputs.read_bytes()
    if (after != before or after_inputs != before_inputs or
            (build / 'distcleaned').exists() or
            state.read_text().splitlines().count('configured') != 1):
        changed = sorted(set(after_inputs.decode().splitlines()) -
                         set(before_inputs.decode().splitlines()))
        raise SystemExit('unrelated WPA/mDNS roots invalidated warm OpenSSH: %r' % changed)
    # The imported OpenSSH root itself remains a real configure input.
    (build / 'distcleaned').unlink(missing_ok=True)
    gnu_source.unlink()
    gnu_source.symlink_to(openssh_roots[1], target_is_directory=True)
    result = subprocess.run(command + ['EMBER_OPENSSH_SOURCE=' + str(openssh_roots[1])],
                            cwd=work, env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    if (build / 'fixture.o').stat().st_ino == before[2]:
        raise SystemExit('changed imported OpenSSH source reused a stale target object')
    if state.read_text().splitlines().count('configured') != 2:
        raise SystemExit('changed imported OpenSSH source did not reconfigure')
    if not (build / 'distcleaned').exists():
        raise SystemExit('changed imported OpenSSH source skipped configure invalidation')
print('PASS: selected configure/make, incremental GNU target compile')
