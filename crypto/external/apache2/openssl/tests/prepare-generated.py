#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check target compiler routing and generation reuse by the SDK input key."""
import argparse
import json
import os
import shutil
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
for name in ('source', 'cc', 'perl', 'gmake', 'ar', 'ranlib', 'shell', 'path', 'work-dir'):
    parser.add_argument('--' + name, required=True)
parser.add_argument('--git-path', help='PATH of the SDK-selected Git used in the fixture context')
parser.add_argument('--jobs', type=int, default=4)
args = parser.parse_args()
git_path = args.git_path or args.path
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
                             'flags': [], 'common_sources': {}, 'environment': {'PATH': git_path}}}
    context_file = obj / '.prebuild-sdk-context.json'
    context_file.write_text(json.dumps(context))
    output = obj / 'generated'
    cmd = [sys.executable, '-B', str(tool), '--source', args.source, '--object-root', str(obj),
           '--output', str(output), '--arch', 'aarch64', '--cpu', 'aarch64', '--triplet', 'aarch64--netbsd', '--family', 'clang', '--cc', args.cc,
           '--sysroot', str(sysroot), '--perl', args.perl, '--gmake', args.gmake,
           '--ar', args.ar, '--ranlib', args.ranlib, '--shell', args.shell,
           '--path', args.path, '--tmpdir', str(root), '--jobs', str(args.jobs)]
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
    header.unlink()
    (output / 'crypto.mk').unlink()
    assert 'Configuring OpenSSL' in run().stdout
    assert header.read_bytes() == old_bytes and (output / 'crypto.mk').is_file()
    assert json.loads((output / '.complete').read_text())['key'] == first['key']
    old_mtime = header.stat().st_mtime_ns
    assert not run(reuse=True).stdout
    (sysroot / 'usr/include/sys').mkdir(parents=True)
    (sysroot / 'usr/include/sys/types.h').write_text('/* changed target header */\n')
    context['sources']['os']['commit'] = '2' * 40
    context_file.write_text(json.dumps(context))
    assert not run(reuse=True).stdout
    assert first['key'] == json.loads((output / '.complete').read_text())['key']
    context['signature']['compiler']['path'] = args.cc + '-changed'
    context_file.write_text(json.dumps(context))
    assert 'Configuring OpenSSL' in run().stdout
    second = json.loads((output / '.complete').read_text())
    assert first['key']['inputs'] != second['key']['inputs']
    assert header.read_bytes() == old_bytes and header.stat().st_mtime_ns == old_mtime

# Git blob identities are stable when already-tested dirty bytes are committed.
with tempfile.TemporaryDirectory(dir=args.work_dir) as directory:
    git = shutil.which('git', path=git_path)
    assert git
    repo = Path(directory)
    subprocess.run([git, 'init', '-q', str(repo)], check=True)
    for name in module.GENERATOR_FILES:
        (repo / name).write_text(name)
    def git_run(*cmd):
        subprocess.run([git, '-C', str(repo), '-c', 'user.name=SDK Test',
                        '-c', 'user.email=test@example.invalid', *cmd],
                       check=True, stdout=subprocess.DEVNULL)
    git_run('add', *module.GENERATOR_FILES)
    git_run('commit', '-qm', 'initial')
    module.TOOLS = repo
    initial = module.generator_identity(git_path)
    for name in module.GENERATOR_FILES:
        (repo / name).write_text(name + ' changed')
        changed = module.generator_identity(git_path)
        assert changed[name] != initial[name]
        assert all(changed[n] == initial[n] for n in module.GENERATOR_FILES if n != name)
        git_run('add', name)
        assert module.generator_identity(git_path) == changed
        git_run('commit', '-qm', 'change')
        assert module.generator_identity(git_path) == changed
        initial = changed
    source = repo / 'source'; source.mkdir()
    prepared_file = source / 'emberbsd-prepared-source.json'
    prepared = dict(format='emberbsd-prepared-source', version=1, package='security/openssl',
                    identity=dict(effective=dict(api=3, effective_package_sha256='old', legacy_api2_sha256='old')))
    prepared_file.write_text(json.dumps(prepared))
    obj = repo / 'obj'; obj.mkdir()
    assert module.input_identity(source, obj) is None
    signature = dict(objects=str(obj), compiler=dict(version='1'), native_toolset=dict(perl='1'),
                     runtime=dict(manifest=dict(triplet='a'), inventory=dict(builtins='1')),
                     target=dict(arch='a'), flags=[], common_sources={}, environment=dict(PATH=git_path))
    context = dict(format='emberbsd-sysroot-object-context', version=1,
                   sources=dict(os=dict(commit='old')), signature=signature)
    context_file = obj / '.prebuild-sdk-context.json'
    def identity():
        context_file.write_text(json.dumps(context))
        return module.input_identity(source, obj)
    first = identity()
    assert first is not None
    original_path = os.environ.get('PATH')
    os.environ['PATH'] = str(repo / 'no-git')
    try:
        assert identity() == first
    finally:
        if original_path is None:
            del os.environ['PATH']
        else:
            os.environ['PATH'] = original_path
    signature['environment']['PATH'] = str(repo / 'no-git')
    assert identity() is None
    signature['environment']['PATH'] = git_path
    context['sources']['os']['commit'] = 'new'
    signature['common_sources']['unrelated'] = 'changed'
    assert identity() == first
    for name in ('compiler', 'native_toolset', 'target'):
        signature[name]['changed'] = True
        assert identity() != first
        del signature[name]['changed']
    signature['runtime']['inventory']['builtins'] = '2'
    assert identity() != first
    signature['runtime']['inventory']['builtins'] = '1'
    prepared['identity']['effective']['legacy_api2_sha256'] = 'new'
    prepared_file.write_text(json.dumps(prepared))
    assert identity() == first
    prepared['identity']['effective']['effective_package_sha256'] = 'new'
    prepared_file.write_text(json.dumps(prepared))
    assert identity() != first

print('PASS: target compiler and generation input-key reuse')
