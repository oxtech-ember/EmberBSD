#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Run the actual AArch64 assembly-constant generator with routed tools."""
import argparse
from pathlib import Path
import re
import shlex
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
for name in ('sdk-source', 'host-prefix', 'work'):
    parser.add_argument('--' + name, required=True, type=Path)
parser.add_argument('--mkdep', required=True, type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
work = args.work.resolve()
work.mkdir(exist_ok=False)
(work / 'tmp').mkdir()
sys.path.insert(0, str(args.sdk_source.resolve() / 'src'))
from emberbsd_sdk.native_os import select_native_os_tools
from emberbsd_sdk.native_os_routing import ensure_native_os_routing
from emberbsd_sdk.inventory import sha256_file
native = select_native_os_tools(str(args.host_prefix.resolve()), str(source))
contract = source / 'tools/native-tools/contract'
routing = work / 'commands'
ensure_native_os_routing(native, contract_path=str(contract),
    contract_sha256=sha256_file(str(contract)), destination=str(routing))
fixture = ('quote typedef __SIZE_TYPE__ size_t;\n'
           'ifndef __aarch64__\nquote #error lost AArch64 target\nendif\n'
           'define FIXTURE_POINTER_BYTES sizeof(void *)\n'
           'define FIXTURE_CONSTANT 42\n')
(work / 'fixture.cf').write_text(fixture)
env = dict(PATH=str(routing), AWK=native.tool('awk'), LC_ALL='C', TMPDIR=str(work / 'tmp'))
query = subprocess.run([native.tool('make'), '-m', str(source / 'share/mk'),
    '-C', str(source / 'tools'), 'MACHINE=evbarm', 'MACHINE_ARCH=aarch64',
    'TOOLDIR=' + str(work / 'unused-tools'),
    'EXTERNAL_TOOLCHAIN=' + str(args.host_prefix.resolve()),
    '-V', '${GENASSYM_CPPFLAGS}'], env=env, text=True, capture_output=True)
if query.returncode:
    raise SystemExit(query.stdout + query.stderr)
generator_flags = shlex.split(query.stdout)
result = subprocess.run([native.tool('sh'), str(source / 'usr.bin/genassym/genassym.sh'),
    '--', native.tool('cc'), '--target=aarch64--netbsd', '-ffreestanding', '-nostdinc',
    *generator_flags],
    input=fixture, env=env, cwd=work, text=True, capture_output=True)
(work / 'generator.log').write_text(result.stdout + result.stderr)
if result.returncode:
    raise SystemExit('actual genassym failed; inspect ' + str(work / 'generator.log'))
for name, value in (('FIXTURE_POINTER_BYTES', 8), ('FIXTURE_CONSTANT', 42)):
    if not re.search(r'^#define\s+' + name + r'\s+' + str(value) + r'\s*$', result.stdout, re.M):
        raise SystemExit('genassym lost the expected ' + name)
(work / 'dependency.h').write_text('#define GENASSYM_DEPENDENCY 1\n')
dependency_fixture = 'quote #include "dependency.h"\n' + fixture
output = work / 'assym.dep'
result = subprocess.run([native.tool('sh'), str(source / 'usr.bin/genassym/genassym.sh'),
    '-d', '--', 'CC=' + native.tool('cc'), str(args.mkdep.resolve()), '-f', str(output),
    '--', '--target=aarch64--netbsd', '-march=armv8-a+nofp+nosimd', '-Werror',
    '-I' + str(work), *generator_flags], input=dependency_fixture,
    env=env, cwd=work, text=True, capture_output=True)
(work / 'dependency.log').write_text(result.stdout + result.stderr)
if result.returncode or str(work / 'dependency.h') not in output.read_text():
    raise SystemExit('actual genassym dependency mode failed; inspect ' + str(work / 'dependency.log'))
for mode in ('-dc', '-df'):
    result = subprocess.run([native.tool('sh'), str(source / 'usr.bin/genassym/genassym.sh'),
        mode, '--', '/compiler-must-not-run'], input=fixture,
        env=env, cwd=work, text=True, capture_output=True)
    if result.returncode != 1 or 'usage:' not in result.stderr:
        raise SystemExit('mixed genassym dependency mode was not rejected')
result = subprocess.run([str(routing / 'head'), '-n', '1', str(work / 'fixture.cf')],
                        env=env, text=True, capture_output=True)
if result.returncode or result.stdout != fixture.splitlines()[0] + '\n':
    raise SystemExit('nested configure head command failed')
print('actual AArch64 genassym emits correct pointer/constants; routed head passes')
