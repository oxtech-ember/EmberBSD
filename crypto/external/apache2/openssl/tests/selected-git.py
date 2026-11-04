#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Test OpenSSL generation reuse with an explicitly selected inspection Git."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--git', required=True, help='absolute path to source-inspection Git')
parser.add_argument('--work-dir', required=True)
args = parser.parse_args()
assert os.path.isabs(args.git) and os.access(args.git, os.X_OK)
tool = Path(__file__).resolve().parents[1] / 'tools/prepare-generated.py'
spec = importlib.util.spec_from_file_location('openssl_generator', tool)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory(dir=args.work_dir) as directory:
    root = Path(directory).resolve()
    tools = root / 'tools'
    tools.mkdir()
    module.TOOLS = tools
    for name in module.GENERATOR_FILES:
        (tools / name).write_text(name)

    def git(*arguments):
        subprocess.run([args.git, '-C', str(tools), '-c', 'user.name=SDK Test',
                        '-c', 'user.email=test@example.invalid', *arguments],
                       check=True, stdout=subprocess.DEVNULL)

    git('init', '-q')
    git('add', '.')
    git('commit', '-qm', 'initial')
    source = root / 'source'
    source.mkdir()
    (source / 'Configure').write_text('stub')
    (source / 'emberbsd-prepared-source.json').write_text(json.dumps(dict(
        format='emberbsd-prepared-source', version=1, package='security/openssl')))
    obj = root / 'obj'
    obj.mkdir()
    signature = dict(objects=str(obj), compiler={}, native_toolset={}, runtime={},
                     target={}, flags=[], common_sources={}, environment=dict(PATH='/no/git'))
    (obj / '.prebuild-sdk-context.json').write_text(json.dumps(dict(
        format='emberbsd-sysroot-object-context', version=1, sources={}, signature=signature)))
    assert module.input_identity(source, obj) is None
    first = module.input_identity(source, obj, args.git)
    assert first and first == module.input_identity(source, obj, args.git)
    changed_name = module.GENERATOR_FILES[1]
    (tools / changed_name).write_text('changed')
    changed = module.input_identity(source, obj, args.git)
    assert changed['generators'][changed_name] != first['generators'][changed_name]
    git('add', changed_name)
    assert module.input_identity(source, obj, args.git) == changed
    assert module.input_identity(source, obj, str(root / 'missing-git')) is None

    output = obj / 'generated'
    output.mkdir()
    required = ('crypto.mk', 'crypto-shared.mk', 'default.mk', 'common.mk', 'ssl.mk',
                'ssl-shared.mk', 'apps.mk', 'legacy.mk', 'legacy-module.mk', 'test.mk', 'cli.mk',
                'libcrypto.ld', 'libssl.ld', 'crypto.expsym', 'ssl.expsym', 'providers/legacy.ld',
                'include/openssl/configuration.h', 'include/openssl/opensslv.h', 'configdata.pm')
    for name in required:
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('existing')
    digest = hashlib.sha256(json.dumps(changed, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    executable = sys.executable
    key = dict(source=str(source), inputs=digest, arch='x86_64', cpu='x86_64', python=executable,
               cc=executable, cc_args=[], family='clang', triplet='x86_64--netbsd', sysroot=str(root),
               perl=executable, gmake=executable, ar=executable, ranlib=executable,
               shell=executable, path='/no/git')
    (output / '.owned').write_text(json.dumps(dict(format='emberbsd-openssl-generated',
                                                   object_root=str(obj))))
    marker = output / '.complete'
    marker.write_text(json.dumps(dict(key=key)))
    before = marker.stat().st_mtime_ns
    sys.argv = ['prepare-generated.py', '--source', str(source), '--output', str(output),
                '--object-root', str(obj), '--arch', 'x86_64', '--cpu', 'x86_64',
                '--triplet', 'x86_64--netbsd', '--family', 'clang', '--cc', executable,
                '--sysroot', str(root), '--perl', executable, '--gmake', executable,
                '--ar', executable, '--ranlib', executable, '--shell', executable,
                '--path', '/no/git', '--tmpdir', str(root), '--git', args.git]
    module.main()
    assert marker.stat().st_mtime_ns == before

print('PASS: selected Git, dirty source identity and actual generation no-op')
