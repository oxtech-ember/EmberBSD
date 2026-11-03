#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Check repeated OpenSSL object cleanup without generated make fragments."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--make', type=Path, required=True)
parser.add_argument('--prefix', type=Path, required=True)
parser.add_argument('--openssl-source', type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
env = dict(os.environ, PATH=f'{args.prefix}/bin:{args.prefix}/gnu/bin',
           SHELL=str(args.prefix / 'bin/mksh'))
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp).resolve()
    objects = root / 'objects'
    generated = root / 'generated'
    objects.mkdir()
    generated.mkdir()
    (generated / 'apps.mk').write_text('SRCS=fixture.c\n')
    (objects / 'fixture.o').touch()
    command = [str(args.make), '-j2', '-C',
               str(source / 'crypto/external/apache2/openssl/lib/libapps'),
               f'MAKEOBJDIR={objects}', 'MKCLEANSRC=no',
               f'EMBER_OPENSSL_SOURCE={args.openssl_source}',
               f'EMBER_OPENSSL_GENERATED={generated}',
               f'HOST_SH={args.prefix}/bin/mksh']
    subprocess.run(command + ['clean'], env=env, check=True, stdout=subprocess.DEVNULL)
    assert not (objects / 'fixture.o').exists()
    (generated / 'apps.mk').unlink()
    generated.rmdir()
    for target in ('cleandir', 'clean'):
        (objects / 'orphan.o').touch()
        (objects / 'orphan.d').touch()
        subprocess.run(command + [target], env=env, check=True, stdout=subprocess.DEVNULL)
        assert not (objects / 'orphan.o').exists()
        assert not (objects / 'orphan.d').exists()
    for targets in (['-n', 'all'], ['-n', 'clean', 'all']):
        result = subprocess.run(command + targets, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        assert result.returncode != 0 and 'apps.mk' in result.stderr
    generated.mkdir()
    result = subprocess.run(command + ['cleandir'], env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    assert result.returncode != 0 and 'apps.mk' in result.stderr
print('OpenSSL clean retry and strict build metadata: PASS')
