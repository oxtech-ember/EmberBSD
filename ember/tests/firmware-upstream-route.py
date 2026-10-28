#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the actual pinned upstream wrapper with controlled build tools."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--upstream', required=True)
parser.add_argument('--patch', required=True)
args = parser.parse_args()
sys.path.insert(0, str(Path(os.environ["EMBERBSD_SDK_SOURCE"]) / "src"))
from emberbsd_sdk import host_prefix, native_checks
native = host_prefix.load_host_prefix(os.environ["EMBER_HOST_PREFIX"], required_roles={"cc", "cxx", "cpp", "python", "bash", "sh", "patch", "mkdir", "cp", "uname", "rm", "mv", "cat", "sed", "grep"})
source = Path(args.upstream).read_bytes()
patch = Path(args.patch).read_bytes()
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    (root / 'build.sh').write_bytes(source)
    for path in ('arm-trusted-firmware', 'edk2/BaseTools', 'edk2-platforms', 'bin'):
        (root / path).mkdir(parents=True)
    subprocess.run([native.tool('patch'), '-s', '-t', '-N', '-p1', '-F0', '-d', str(root)],
                   input=patch, check=True)
    (root / 'edk2/edksetup.sh').write_text(':\n')
    def script(name, text):
        path = root / 'bin' / name
        path.write_text('#!' + native.tool('sh') + '\nset -eu\n' + text + '\n')
        path.chmod(0o755)
    script('gmake', 'printf "make %s\\n" "$*" >> "$TRACE"')
    script('git', 'if [ "$1" = describe ]; then echo fixture; fi; exit 0')
    script('build', 'printf "build %s\\n" "$*" >> "$TRACE"\nmkdir -p "$WORKSPACE/Build/RPi5/RELEASE_CLANGDWARF/FV"\nprintf image > "$WORKSPACE/Build/RPi5/RELEASE_CLANGDWARF/FV/RPI_EFI.fd"')
    script('brew', 'echo unwanted >> "$TRACE"; exit 99')
    trace = root / 'trace'
    env = native_checks.check_environment(native, bindings=str(root / 'bindings'))
    env.update(PATH=str(root / 'bin') + os.pathsep + env['PATH'], TRACE=str(trace),
               RPI_BUILD_MAKE=str(root / 'bin/gmake'), RPI_TFA_AR='/selected/llvm-ar',
               RPI_TOOLCHAIN_TAG='CLANGDWARF', CROSS_COMPILE='')
    subprocess.run([native.tool('bash'), str(root / 'build.sh'), '--model', '5',
                    '--tfa-flags', 'CC=/selected/clang OC=/selected/llvm-objcopy',
                    '--edk2-flags', '-D EMBER_CLANG_FIRMWARE_ROOT=/selected/empty'],
                   cwd=root, env=env, check=True, capture_output=True)
    text = trace.read_text()
    assert 'AR=/selected/llvm-ar' in text and 'CC=/selected/clang' in text
    assert 'OC=/selected/llvm-objcopy' in text and '-t CLANGDWARF' in text
    assert 'unwanted' not in text
    assert (root / 'RPI_EFI.fd').read_text() == 'image'
print('pinned wrapper routing passed with controlled tools')
