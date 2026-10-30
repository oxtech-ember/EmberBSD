#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise a cold lib build_install with real generation and tiny readers.

The selected bmake wrapper must use a fresh object tree with OpenSSL obj made.
HOST_PYTHON, HOST_PERL, HOST_GMAKE, HOST_SH and the selected PATH are required.
"""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ('bmake', 'openssl-source', 'work', 'object-parent'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
work = a.work.resolve()
work.mkdir(exist_ok=False)
reader = work / 'reader'
reader.mkdir()
(reader / 'Makefile').write_text('dependall install:\n\t@test -s "${EMBER_OPENSSL_GENERATED}/.complete"\n\t@echo READER_OK:${.TARGET}\n')
(work / 'dest').mkdir()
if (a.object_parent / 'generated').exists():
    raise SystemExit('object-parent must be cold')
env = os.environ.copy()
env['EMBER_OPENSSL_SOURCE'] = str(a.openssl_source.resolve(strict=True))
for name in ('EMBER_OPENSSL_GENERATED', 'EMBER_OPENSSL_GENERATION_READY'):
    env.pop(name, None)
lib = Path(__file__).resolve().parents[5] / 'lib'
result = subprocess.run([str(a.bmake.resolve(strict=True)), '-j2', '-C', str(lib),
                         'DESTDIR=' + str(work / 'dest'), 'SUBDIR=' + str(reader),
                         'build_install'], env=env, text=True, capture_output=True)
(work / 'build.log').write_text(result.stdout + result.stderr)
if result.returncode or not (a.object_parent / 'generated/.complete').is_file():
    raise SystemExit('cold generation failed; see build.log')
if 'READER_OK:dependall' not in result.stdout or 'READER_OK:install' not in result.stdout:
    raise SystemExit('recursive readers did not observe generation; see build.log')
print('PASS: cold build_install generates before recursive readers')
