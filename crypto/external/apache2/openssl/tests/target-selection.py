#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check OS CPU normalization and compiler ABI flag propagation."""
import argparse
import importlib.util
import json
import os
import sys
from pathlib import Path
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bmake', type=Path, required=True)
p.add_argument('--clang', type=Path, required=True)
p.add_argument('--work-dir', type=Path, required=True)
p.add_argument('--os-wrapper', type=Path, required=True)
p.add_argument('--openssl-source', type=Path, required=True)
p.add_argument('--perl', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[5]
helper = Path(__file__).resolve().parents[1] / 'tools/prepare-generated.py'
spec = importlib.util.spec_from_file_location('prepare_generated', helper)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
cases = {'aarch64': 'BSD-aarch64', 'riscv32': 'BSD-riscv32',
         'riscv64': 'BSD-riscv64', 'powerpc': 'BSD-ppc',
         'powerpc64': 'BSD-ppc64', 'powerpc64le': 'BSD-ppc64le',
         'x86_64': 'BSD-x86_64', 'i386': 'BSD-x86-elf',
         'sparc64': 'BSD-sparc64', 'sh3el': 'BSD-generic32'}
with tempfile.TemporaryDirectory(dir=a.work_dir) as tmp:
    fixture = Path(tmp) / 'cpu.mk'
    fixture.write_text('.include <bsd.own.mk>\n')
    for arch, expected in cases.items():
        cpu = subprocess.check_output([str(a.bmake.resolve(strict=True)), '-m', str(root / 'share/mk'),
            '-f', str(fixture), 'MACHINE_ARCH=' + arch, '-V', '${MACHINE_CPU}'], text=True, cwd=tmp).strip()
        if module.configured_target(arch, cpu) != expected:
            raise AssertionError((arch, cpu, expected))
    assert module.configured_target('unknown', 'unknown') is None
    # Compare the real bsd.sys COMPILE.c shell argv with the OpenSSL parent
    # recipe's forwarded argv, including a quoted value with a space.
    capture = Path(tmp) / 'capture-tool'
    capture.write_text('#!' + sys.executable + '\nimport json,sys\nfrom pathlib import Path\n'
                       + 'Path(' + repr(str(Path(tmp) / 'args.json')) + ').write_text(json.dumps(sys.argv))\n')
    capture.chmod(0o755)
    fixture.write_text('.include <bsd.own.mk>\n.include <bsd.sys.mk>\nCFLAGS+= ${COPTS}\n'
                       'capture:\n\t${COMPILE.c} /dev/null -o /dev/null\n')
    flags = ['COPTS=-DSPACE="hello world"', 'CPUFLAGS=-mabi=n32']
    env = os.environ.copy()
    env.update(MACHINE='evbarm', MACHINE_ARCH='aarch64')
    subprocess.run([str(a.bmake.resolve(strict=True)), '-m', str(root / 'share/mk'), '-f', str(fixture),
                    'CC=' + str(capture), *flags, 'capture'], cwd=tmp, env=env,
                   text=True, capture_output=True, check=True)
    compile_argv = json.loads((Path(tmp) / 'args.json').read_text())
    env.update(EMBER_OPENSSL_SOURCE=str(a.openssl_source.resolve(strict=True)),
               HOST_PERL=str(a.perl.resolve(strict=True)))
    parent = Path(__file__).resolve().parents[1]
    subprocess.run([str(a.os_wrapper.resolve(strict=True)), '-C', str(parent),
                    'HOST_PYTHON=' + str(capture), *flags, 'openssl-generated'], cwd=tmp, env=env,
                   text=True, capture_output=True, check=True)
    forwarded = json.loads((Path(tmp) / 'args.json').read_text())
    selected = []
    for name in ('--cc-flags', '--cc-options'):
        selected.extend(shlex.split(next(x.split('=', 1)[1] for x in forwarded if x.startswith(name + '='))))
    relevant = lambda args: [x for x in args if 'SPACE' in x or 'mabi' in x]
    assert relevant(compile_argv) == relevant(selected) == ['-mabi=n32', '-DSPACE=hello world']
    for abi, size in [('n32', '4'), ('64', '8')]:
        command = shlex.split(module.compiler_command(str(a.clang.resolve(strict=True)), 'clang',
                                                     'mips64el--netbsd', tmp, ['-mabi=' + abi]))
        macros = subprocess.check_output([*command, '-dM', '-E', '-x', 'c', '/dev/null'], text=True)
        assert '#define __SIZEOF_POINTER__ ' + size + '\n' in macros
    assert shlex.split(module.compiler_command('/path with space/cc', 'gcc', 'mips--netbsd',
                                               '/sysroot with space', ['-mabi=n32'])) == [
        '/path with space/cc', '-mabi=n32', '--sysroot=/sysroot with space']
print('PASS: OS target selection and compiler ABI flags')
