#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Exercise the Waf adapter's incremental configuration and failure paths."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--bmake', required=True)
parser.add_argument('--python', required=True)
parser.add_argument('--shell', required=True)
parser.add_argument('--tmpdir', required=True)
args = parser.parse_args()
source_root = Path(__file__).resolve().parents[2]
for tool in (args.bmake, args.python, args.shell):
    assert Path(tool).is_absolute()

with tempfile.TemporaryDirectory(dir=args.tmpdir, prefix='waftarget-test-') as tmp:
    root = Path(tmp)
    source = root / 'source'
    (source / 'buildtools/bin').mkdir(parents=True)
    (source / 'wscript').write_text('first configuration\n')
    (source / 'config').mkdir()
    (source / 'config/settings.py').write_text('first\n')
    (source / 'buildtools/bin/waf').write_text('''import os, pathlib, sys
out = pathlib.Path(next(x[6:] for x in sys.argv if x.startswith('--out=')))
phase = sys.argv[1]
with (out / 'calls').open('a') as f:
    f.write(phase + ' ' + os.environ.get('CFLAGS', '') + ' / ' + os.environ.get('ADDITIONAL_CFLAGS', '') + '\\n')
if phase == 'configure' and (out / 'fail-once').exists():
    (out / 'fail-once').unlink()
    raise SystemExit(1)
''')
    answers = root / 'answers'
    answers.write_text('first\n')
    obj = root / 'obj'
    makefile = root / 'Makefile'
    makefile.write_text(f'''NETBSDSRCDIR= {source_root}
WAF_SOURCE= {source}
WAF_CONFIGURE_INPUTS= wscript config
WAF_OBJDIR= {obj}
HOST_PYTHON= {args.python}
HOST_SH= {args.shell}
CC= /selected/target-cc
AR= /selected/target-ar
RANLIB= /selected/target-ranlib
DESTDIR= /target-root
CFLAGS= -O2 -Werror
WAF_ANSWERS_SOURCE= {answers}
WAF_CONFIGURE_ARGS= --cross-answers=${{WAF_OBJDIR}}/answers
.include "{source_root / 'share/mk/bsd.waftarget.mk'}"
''')
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join((str(Path(args.shell).parent), str(Path(args.shell).parent.parent / 'gnu/bin')))
    env['TMPDIR'] = args.tmpdir
    def run(*extra, ok=True):
        p = subprocess.run([args.bmake, '-f', str(makefile), *extra, 'waf-target-build'],
                           env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert (p.returncode == 0) == ok, p.stdout.decode(errors='replace')
        return p
    def calls():
        return (obj / 'calls').read_text().splitlines()
    run('-n')
    assert not obj.exists()
    run()
    assert [x.split()[0] for x in calls()] == ['configure', 'build']
    assert ' -Werror' not in calls()[0].split(' / ')[0]
    assert '-Werror' in calls()[1].split(' / ')[1]
    run()
    assert [x.split()[0] for x in calls()] == ['configure', 'build', 'build']
    answers.write_text('second\n')
    run()
    assert [x.split()[0] for x in calls()][-2:] == ['configure', 'build']
    (obj / 'fail-once').touch()
    run('CFLAGS=-O3 -Werror', ok=False)
    assert not (obj / '.configured').exists()
    run('CFLAGS=-O3 -Werror')
    assert [x.split()[0] for x in calls()][-3:] == ['configure', 'configure', 'build']
    before = len(calls())
    os.utime(source / 'config/settings.py', None)
    run('CFLAGS=-O3 -Werror')
    assert len(calls()) == before + 1 and calls()[-1].startswith('build ')
    (source / 'config/settings.py').write_text('changed\n')
    run('CFLAGS=-O3 -Werror')
    assert [x.split()[0] for x in calls()][-2:] == ['configure', 'build']
    (source / 'wscript').write_text('changed configuration\n')
    run('CFLAGS=-O3 -Werror')
    assert [x.split()[0] for x in calls()][-2:] == ['configure', 'build']
print('Waf adapter incremental/failure/dry-run: PASS')
