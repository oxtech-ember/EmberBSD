#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted GCC 16 runtime helper set regression.
"""Check that only the GCC profile installs and lists ember-runtime-install.

The helper installs the GCC 16 base runtime package, and sysinst calls it only
with EMBER_BASE_RUNTIME_GCC16. The test asks libexec/ember-desktop for its
SCRIPTS and runs distrib/sets/makeflist for the base sets, once for the Clang
profile and once for the GCC profile with a fixture runtime tree: the helper
must be absent from the Clang profile and present in the GCC profile.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

HELPER = 'ember-runtime-install'
RUNTIME_FILES = ('lib/gcc/aarch64--netbsd/16.2.0/' + name for name in
                 ('crtbegin.o', 'crtbeginS.o', 'crtend.o', 'crtendS.o', 'libgcc.a', 'libgcc_eh.a'))
RUNTIME_FILES = (*RUNTIME_FILES, 'lib/libgcc_s.so.1', 'lib/libstdc++.so', 'lib/libstdc++.so.7', 'lib/libstdc++.a',
                 'lib/libsupc++.a', 'include/c++/vector', 'include/c++/aarch64--netbsd/bits/c++config.h')


def environment(args, os_root, root, profile):
    env = {'PATH': str(Path(args.make).parent) + ':/usr/bin:/bin', 'HOME': str(root), 'MAKE': args.make,
           'AWK': args.awk, 'MAKESYSPATH': str(os_root / 'share/mk'), 'MAKECONF': '/dev/null',
           'MACHINE': 'evbarm', 'MACHINE_ARCH': 'aarch64', 'NETBSDSRCDIR': str(os_root),
           'EMBER_COMPILER_FAMILY': profile, 'TMPDIR': str(root)}
    if profile == 'gcc':
        runtime = root / 'gcc16'
        for name in RUNTIME_FILES:
            (runtime / name).parent.mkdir(parents=True, exist_ok=True)
            (runtime / name).write_text('')
        env |= {'EMBER_EXTERNAL_GCC_RUNTIME': str(runtime), 'EXTERNAL_TOOLCHAIN': str(root / 'toolchain'),
                'USETOOLS': 'yes', 'MKGCC': 'no'}
    return env


def run(command, env, cwd):
    result = subprocess.run(command, env=env, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0 or result.stderr.strip():
        raise SystemExit(f'{command[0]} failed for EMBER_COMPILER_FAMILY={env["EMBER_COMPILER_FAMILY"]}:\n'
                         + result.stderr)
    return result.stdout


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--make', required=True, help='bmake from the selected host prefix')
    cli.add_argument('--awk', required=True, help='nawk from the selected host prefix')
    cli.add_argument('--os', type=Path, default=Path(__file__).resolve().parents[2])
    args = cli.parse_args()
    os_root = args.os.resolve()
    with tempfile.TemporaryDirectory(prefix='ember-runtime-helper-') as temporary:
        root = Path(temporary).resolve()
        for profile, wanted in (('clang', False), ('gcc', True)):
            env = environment(args, os_root, root, profile)
            scripts = run([args.make, '-C', str(os_root / 'libexec/ember-desktop'), '-V', 'SCRIPTS'],
                          env, root).split()
            listed = run(['sh', str(os_root / 'distrib/sets/makeflist'), 'base'], env, root).splitlines()
            if 'ember-desktop-install' not in scripts or './usr/libexec/ember-desktop-install' not in listed:
                raise SystemExit(f'{profile} profile lost the desktop helpers')
            for name, present in (('SCRIPTS', HELPER in scripts),
                                  ('base set list', './usr/libexec/' + HELPER in listed)):
                if present != wanted:
                    raise SystemExit(f'{profile} profile {"lacks" if wanted else "has"} {HELPER} in {name}')
    print(f'PASS: only the GCC profile installs and lists {HELPER}')


if __name__ == '__main__':
    main()
