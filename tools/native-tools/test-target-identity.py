#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Create and query a real cross make wrapper without compiling tools."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
for name in ('sdk-source', 'host-prefix', 'work'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
work = args.work.resolve()
work.mkdir(exist_ok=False)
runner = args.sdk_source.resolve() / 'scripts/run-native-os.py'
command = [sys.executable, '-B', str(runner), '--host-prefix', str(args.host_prefix.resolve()),
           '--compiler-family', 'clang', '--source', str(source),
           '--routing', str(work / 'native-tools'), '--cwd', str(source)]
environment = os.environ.copy()
for name in ('MAKEFLAGS', 'MFLAGS', 'MACHINE', 'MACHINE_ARCH', 'EMBER_HOST_BFD_CONFIG'):
    environment.pop(name, None)
# Override the host make's compiled-in architecture without building nbmake.
result = subprocess.run(command + ['--log', str(work / 'build.log'), '--build-sh', '--', '-U', '-u', '-m', 'evbarm',
    '-a', 'aarch64', '-O', str(work / 'obj'), '-T', str(work / 'tools'),
    '-D', str(work / 'dest'), '-V', 'MKOBJDIRS=no', 'makewrapper'],
    env=environment, text=True, capture_output=True)
(work / 'wrapper.log').write_text(result.stdout + result.stderr)
if result.returncode:
    raise SystemExit('wrapper creation failed; inspect ' + str(work / 'wrapper.log'))
wrapper = work / 'tools/bin/nbmake-evbarm'
result = subprocess.run(command + ['--capture', '--shell', '--', str(wrapper), '-C', str(source / 'tools'),
    '-V', '${MACHINE}', '-V', '${MACHINE_ARCH}', '-V', '${MACHINE_GNU_PLATFORM}'],
    env=environment, text=True, capture_output=True)
(work / 'query.log').write_text(result.stdout + result.stderr)
if result.returncode or result.stdout.splitlines() != ['evbarm', 'aarch64', 'aarch64--netbsd']:
    raise SystemExit('cross wrapper lost target identity:\n' + result.stdout + result.stderr)
print('real cross wrapper retains evbarm/aarch64/aarch64--netbsd')
