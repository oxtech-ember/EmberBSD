#!/usr/bin/env python3
"""Exercise the current checkflist against controlled producer failures."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--os', required=True, type=Path)
p.add_argument('--prefix', required=True, type=Path)
p.add_argument('--checkflist', type=Path, help='optional alternate checkflist source')
a = p.parse_args()
source = a.os / 'distrib/sets'
prefix = a.prefix.resolve()
with tempfile.TemporaryDirectory(prefix='checkflist-fixture-') as temp:
    root = Path(temp)
    scripts = root / 'scripts'
    scripts.mkdir()
    for name in ('checkflist', 'sets.subr', 'mkvars.mk'):
        shutil.copyfile(a.checkflist if name == 'checkflist' and a.checkflist else source / name,
                        scripts / name)
    def shell(name, body):
        path = root / name
        path.write_text('#!' + str(prefix / 'bin/mksh') + '\n' + body + '\n')
        path.chmod(0o755)
        return str(path)
    fake_make = shell('make', "printf '%s\\n' 'MACHINE=aarch64' 'MACHINE_ARCH=aarch64' 'MACHINE_CPU=aarch64' 'OBJECT_FMT=ELF' >&9")
    (scripts / 'makeflist').write_text('printf "%s\\n" "${FIXTURE_FLIST:-.}"\nexit "${FIXTURE_FLIST_EXIT:-0}"\n')
    fail = shell('fail', 'exit 23')
    empty_filtered = shell('filtered', "printf '%s\\n' './METALOG'")
    metalog = root / 'metalog'
    metalog.write_text('.\n')
    env = dict(os.environ, PATH=str(prefix / 'bin') + ':' + str(prefix / 'gnu/bin'),
               MAKE=fake_make, HOST_SH=str(prefix / 'bin/mksh'), LC_ALL='C',
               NETBSDSRCDIR=str(a.os.resolve()))
    cases = [
        ('normal', {}, [], True),
        ('filtered-empty', {'FIND': empty_filtered, 'FIXTURE_FLIST': './METALOG'}, [], True),
        ('missing-comm', {'COMM': str(root / 'absent')}, [], False),
        ('failing-comm', {'COMM': fail}, [], False),
        ('failing-comm-allowed', {'COMM': fail}, ['-e', '-m'], False),
        ('failing-find', {'FIND': fail}, ['-e', '-m'], False),
        ('failing-makeflist', {'FIXTURE_FLIST_EXIT': '23'}, ['-e', '-m'], False),
        ('failing-sort', {'SORT': fail}, ['-e', '-m'], False),
        ('failing-filter', {'EGREP': fail}, ['-e', '-m'], False),
        ('failing-awk', {'AWK': fail}, ['-M', str(metalog), '-e', '-m'], False),
    ]
    failed = 0
    for name, overrides, flags, expected in cases:
        dest = root / ('dest-' + name)
        dest.mkdir()
        e = env | overrides | {'DESTDIR': str(dest)}
        r = subprocess.run([str(prefix / 'bin/mksh'), str(scripts / 'checkflist'), *flags],
                           env=e, capture_output=True, text=True)
        ok = (r.returncode == 0) == expected
        print(f'{name}: {"PASS" if ok else "FAIL"} (exit={r.returncode}, expected={"success" if expected else "failure"})')
        if not ok:
            failed += 1
            print(r.stderr[-600:])
    raise SystemExit(bool(failed))
