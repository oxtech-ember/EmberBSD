#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check target compiler routing and generation reuse by the SDK input key."""
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
    root = Path(directory).resolve()
    sysroot = root / 'sysroot'
    sysroot.mkdir()
    obj = root / 'obj'
    obj.mkdir()
    context = {'format': 'emberbsd-sysroot-object-context', 'version': 1,
               'sources': {'os': {'commit': '1' * 40}},
               'signature': {'objects': str(obj), 'compiler': {'path': args.cc},
                             'native_toolset': {'perl': args.perl, 'make': args.gmake},
                             'runtime': {}, 'target': {'arch': 'aarch64'},
                             'flags': [], 'common_sources': {}}}
    context_file = obj / '.prebuild-sdk-context.json'
    context_file.write_text(json.dumps(context))
    output = obj / 'generated'
    cmd = [sys.executable, '-B', str(tool), '--source', args.source, '--object-root', str(obj),
           '--output', str(output), '--arch', 'aarch64', '--cpu', 'aarch64', '--triplet', 'aarch64--netbsd', '--family', 'clang', '--cc', args.cc,
           '--sysroot', str(sysroot), '--perl', args.perl, '--gmake', args.gmake,
           '--ar', args.ar, '--ranlib', args.ranlib, '--shell', args.shell,
           '--path', args.path, '--tmpdir', str(root), '--jobs', '4']
    def run(reuse=False):
        command = cmd
        if reuse:
            # A matching key must not walk or open any payload, including tools.
            roots = [str(Path(args.source).resolve()) + '/', str(sysroot) + '/', str(output) + '/']
            allowed = [str(Path(args.source).resolve() / 'emberbsd-prepared-source.json'),
                       str(output / '.owned'), str(output / '.complete')]
            executables = [args.cc, args.perl, args.gmake, args.ar, args.ranlib, args.shell]
            guard = '''import runpy, sys
def audit(event, values):
    if event == 'pathlib.Path.rglob':
        raise AssertionError('matching key walked a tree')
    if event == 'open' and isinstance(values[0], str):
        name = values[0]
        if name not in ALLOWED and (name in EXECUTABLES or any(name.startswith(root) for root in ROOTS)):
            raise AssertionError('matching key read a payload')
sys.addaudithook(audit)
sys.argv = sys.argv[1:]
runpy.run_path(sys.argv[0], run_name='__main__')
'''
            scope = f'ROOTS={roots!r}\nALLOWED={allowed!r}\nEXECUTABLES={executables!r}\n'
            command = [sys.executable, '-B', '-c', scope + guard, *cmd[2:]]
        return subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    run()
    config = (output / 'configdata.pm').read_text()
    assert '--target=aarch64--netbsd --sysroot=' + str(sysroot) in config
    first = json.loads((output / '.complete').read_text())
    header = output / 'include/openssl/opensslv.h'
    old_bytes, old_mtime = header.read_bytes(), header.stat().st_mtime_ns
    assert not run(reuse=True).stdout
    (sysroot / 'usr/include/sys').mkdir(parents=True)
    (sysroot / 'usr/include/sys/types.h').write_text('/* changed target header */\n')
    context['sources']['os']['commit'] = '2' * 40
    context_file.write_text(json.dumps(context))
    assert 'Configuring OpenSSL' in run().stdout
    second = json.loads((output / '.complete').read_text())
    assert first['key']['inputs'] != second['key']['inputs']
    assert header.read_bytes() == old_bytes and header.stat().st_mtime_ns == old_mtime
print('PASS: target compiler and generation input-key reuse')
