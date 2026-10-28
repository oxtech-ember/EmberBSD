#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check TDB's Waf output and debug staging without a full build."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('bmake', 'python', 'shell', 'objcopy', 'tools', 'tmpdir', 'elf', 'changed-elf', 'archive'):
    p.add_argument('--' + name, required=True)
a = p.parse_args()
os_root = Path(__file__).resolve().parents[2]
for tool in (a.bmake, a.python, a.shell, a.objcopy):
    assert Path(tool).is_absolute()
with tempfile.TemporaryDirectory(dir=a.tmpdir, prefix='tdb-producer-') as tmp:
    root = Path(tmp)
    source, obj, dest = root / 'source', root / 'obj', root / 'dest'
    (source / 'buildtools/bin').mkdir(parents=True)
    (source / 'include').mkdir()
    (source / 'third_party/waf').mkdir(parents=True)
    (source / 'lib/replace').mkdir(parents=True)
    (source / 'include/tdb.h').write_text('/* fixture */\n')
    (source / 'wscript').write_text('fixture\n')
    (source / 'choice').write_text('first')
    obj.mkdir()
    (source / 'buildtools/bin/waf').write_text(f'''import pathlib, shutil, sys
out = pathlib.Path(next(x[6:] for x in sys.argv if x.startswith('--out='))) / 'default'
if sys.argv[1] == 'build':
    out.mkdir(parents=True, exist_ok=True)
    chosen = {a.elf!r} if pathlib.Path({str(source / 'choice')!r}).read_text() == 'first' else {a.changed_elf!r}
    shutil.copyfile(chosen, out / 'libtdb.so')
    shutil.copyfile({a.archive!r}, out / 'libtdb.a')
''')
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join((str(Path(a.shell).parent), str(Path(a.shell).parent.parent / 'gnu/bin')))
    env['TMPDIR'] = a.tmpdir
    env['MAKEOBJDIR'] = str(obj)
    command = [a.bmake, '-r', '-m', str(os_root / 'share/mk'), '-C', str(os_root / 'external/bsd/tdb/lib'),
               'MAKECONF=/dev/null', '_SRC_TOP_=', 'NETBSDSRCDIR=' + str(os_root),
               'MACHINE=evbarm', 'MACHINE_ARCH=aarch64', 'USETOOLS=yes', 'TOOLDIR=' + a.tools,
               'DESTDIR=' + str(dest), 'EMBER_TDB_SOURCE=' + str(source), 'HOST_SH=' + a.shell,
               'HOST_PYTHON=' + a.python, 'OBJCOPY=' + a.objcopy, 'MKDEBUG=yes', 'CFLAGS=-O2 -g -Werror',
               '-j2', 'all']
    def run(ok=True):
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert (result.returncode == 0) == ok, result.stdout.decode(errors='replace')
    def digest(path):
        return hashlib.sha256(path.read_bytes()).digest()
    run()
    lib, debug = obj / 'libtdb.so.1.4.15', obj / 'libtdb.so.1.4.15.debug'
    assert lib.is_file() and debug.is_file()
    first = digest(lib)
    debug_mtime = debug.stat().st_mtime_ns
    run()
    assert digest(lib) == first and debug.stat().st_mtime_ns == debug_mtime
    (source / 'choice').write_text('second')
    run()
    assert digest(lib) != first
    assert debug.is_file()
    # Fail the link-stage copy after downstream outputs have been removed.
    mocks = root / 'mock-tools'
    mocks.mkdir()
    cp = mocks / 'cp'
    real_cp = Path(a.shell).parent / 'gcp'
    cp.write_text(f'#!{a.shell}\ncase "$*" in *.link.tmp*) exit 1;; esac\nexec {real_cp} "$@"\n')
    cp.chmod(0o755)
    env['PATH'] = str(mocks) + os.pathsep + env['PATH']
    (source / 'choice').write_text('first')
    run(ok=False)
    assert not lib.exists() and not debug.exists()
    cp.unlink()
    run()
    assert digest(lib) == first and debug.is_file()
    # A failed final copy must leave no partial output accepted on retry.
    for target in (lib, obj / 'libtdb.a'):
        target.unlink()
        cp.write_text(f'#!{a.shell}\ncase "$*" in *{target.name}*) printf partial > "$3"; exit 1;; esac\nexec {real_cp} "$@"\n')
        cp.chmod(0o755)
        run(ok=False)
        assert not target.exists()
        cp.unlink()
        run()
        assert target.is_file() and target.read_bytes() != b'partial'
print('TDB staged ELF/debug reuse, change and retry: PASS')
