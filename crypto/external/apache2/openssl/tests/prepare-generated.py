#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check target compiler routing and same-path sysroot cache invalidation."""
import argparse
import json
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
for name in ('source', 'cc', 'perl', 'gmake', 'ar', 'ranlib', 'shell', 'path', 'work-dir'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
tool = Path(__file__).resolve().parents[1] / 'tools/prepare-generated.py'
spec = importlib.util.spec_from_file_location('openssl_generator', tool)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
assert module.compiler_command('/prefix/bin/gcc', 'gcc', 'aarch64--netbsd', '/sysroot') == '/prefix/bin/gcc --sysroot=/sysroot'
assert module.compiler_command('/prefix/bin/clang', 'clang', 'aarch64--netbsd', '/sysroot') == '/prefix/bin/clang --target=aarch64--netbsd --sysroot=/sysroot'
with tempfile.TemporaryDirectory(dir=args.work_dir) as directory:
    root = Path(directory)
    sysroot = root / 'sysroot'
    sysroot.mkdir()
    obj = root / 'obj'
    obj.mkdir()
    output = obj / 'generated'
    cmd = [sys.executable, '-B', str(tool), '--source', args.source, '--object-root', str(obj),
           '--output', str(output), '--arch', 'aarch64', '--cpu', 'aarch64', '--triplet', 'aarch64--netbsd', '--family', 'clang', '--cc', args.cc,
           '--sysroot', str(sysroot), '--perl', args.perl, '--gmake', args.gmake,
           '--ar', args.ar, '--ranlib', args.ranlib, '--shell', args.shell,
           '--path', args.path, '--tmpdir', str(root), '--jobs', '4']
    def run():
        return subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    run()
    config = (output / 'configdata.pm').read_text()
    assert '--target=aarch64--netbsd --sysroot=' + str(sysroot) in config
    first = json.loads((output / '.complete').read_text())
    header = output / 'include/openssl/opensslv.h'
    old_bytes, old_mtime = header.read_bytes(), header.stat().st_mtime_ns
    assert not run().stdout
    (sysroot / 'usr/include/sys').mkdir(parents=True)
    (sysroot / 'usr/include/sys/types.h').write_text('/* changed target header */\n')
    assert 'Configuring OpenSSL' in run().stdout
    second = json.loads((output / '.complete').read_text())
    assert first['key']['inputs'] != second['key']['inputs']
    assert header.read_bytes() == old_bytes and header.stat().st_mtime_ns == old_mtime
    header.chmod(0o600)
    header.write_bytes(b'corrupt\n')
    assert 'Configuring OpenSSL' in run().stdout
    assert header.read_bytes() == old_bytes
print('PASS: target compiler and same-path sysroot cache invalidation')
