#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Exercise the real kernel set target without staging an OS distribution."""
import argparse
import os
import time
from pathlib import Path
import subprocess
import tarfile
import tempfile

parser = argparse.ArgumentParser()
for name in ('source', 'makefile', 'make', 'runner', 'prefix', 'routing'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='kernel-set-only-') as temporary:
    root = Path(temporary)
    obj, release = root / 'objects', root / 'release'
    kernel = obj / 'GENERIC_AMDGPU/netbsd'
    kernel.parent.mkdir(parents=True)
    release.mkdir()
    kernel.write_bytes(b'kernel fixture\n')
    old_time = time.time() - 5
    os.utime(kernel, (old_time, old_time))
    sets = release / 'amd64/binary/sets'
    sets.mkdir(parents=True)
    sibling = sets / 'other.tar.xz'
    sibling.write_bytes(b'other set')
    command = [str(args.prefix / 'bin/python3.15'), '-B', str(args.runner),
        '--host-prefix', str(args.prefix), '--compiler-family', 'clang',
        '--source', str(args.source), '--routing', str(args.routing),
        '--cwd', str(args.source), '--capture', '--shell', '--', str(args.make),
        '-C', str(args.source / 'etc'), '-f', str(args.makefile),
        'RELEASEDIR=' + str(release), 'KERNOBJDIR=' + str(obj),
        'KERNEL_SETS=GENERIC_AMDGPU', 'ALL_KERNELS=GENERIC_AMDGPU',
        'KERNELS_DONE=yes', 'MKDEBUGKERNEL=no']
    def run(*extra):
        return subprocess.run(command + list(extra), capture_output=True, text=True, timeout=90)
    result = run('pack-kernset-GENERIC_AMDGPU')
    assert result.returncode == 0, result.stdout + result.stderr
    archive = sets / 'kern-GENERIC_AMDGPU.tar.xz'
    with tarfile.open(archive, 'r:xz') as tar:
        files = [member for member in tar if member.isfile()]
        assert len(files) == 1 and files[0].name == './netbsd'
        assert tar.extractfile(files[0]).read() == kernel.read_bytes()
    stamp = (archive.stat().st_ino, archive.stat().st_mtime_ns)
    assert run('pack-kernset-GENERIC_AMDGPU').returncode == 0
    assert (archive.stat().st_ino, archive.stat().st_mtime_ns) == stamp
    assert sibling.read_bytes() == b'other set'
    kernel.unlink()
    variant = kernel.with_name('netbsd.bin')
    variant.write_bytes(b'suffix kernel\n')
    new_time = time.time() + 2
    os.utime(variant, (new_time, new_time))
    result = run('KERNEL_SUFFIXES=bin', 'pack-kernset-GENERIC_AMDGPU')
    assert result.returncode == 0, result.stdout + result.stderr
    with tarfile.open(archive, 'r:xz') as tar:
        files = [member for member in tar if member.isfile()]
        assert len(files) == 1 and files[0].name == './netbsd.bin'
        assert tar.extractfile(files[0]).read() == variant.read_bytes()
    stamp = (archive.stat().st_ino, archive.stat().st_mtime_ns)
    variant.unlink()
    result = run('KERNEL_SUFFIXES=bin', 'pack-kernset-GENERIC_AMDGPU')
    assert result.returncode != 0 and 'Missing kernel:' in result.stdout + result.stderr
    assert (archive.stat().st_ino, archive.stat().st_mtime_ns) == stamp
    assert sibling.read_bytes() == b'other set'
    result = run('-n', 'kernset-GENERIC_AMDGPU')
    assert 'distribution' in result.stdout + result.stderr
    print('PASS: real pax kernel set, no-op, suffix-only, missing input and legacy distribution dependency')
