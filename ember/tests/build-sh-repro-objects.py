#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Exercise build.sh -O with reproducible external Clang settings."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host-prefix', type=Path, required=True)
    parser.add_argument('--native-routing', type=Path, required=True)
    parser.add_argument('--host-bfd-config', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    prefix = args.host_prefix.resolve(strict=True)
    shell = prefix / 'bin/mksh'
    work = args.work_dir.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='build-sh-repro-', dir=work) as tmp:
        root = Path(tmp)
        obj = root / 'not-yet-created-obj'
        tools = root / 'tools'
        (tools / 'bin').mkdir(parents=True)
        env = {'PATH': str(prefix / 'bin') + ':' + str(prefix / 'gnu/bin'),
               'HOME': tmp, 'TMPDIR': tmp, 'LC_ALL': 'C', 'MAKECONF': '/dev/null',
               'EMBER_NATIVE_TOOLS': str(args.native_routing.resolve(strict=True))}
        command = [str(shell), '-x', str(source / 'build.sh'), '-U', '-u',
                   '-m', 'evbarm', '-a', 'aarch64', '-O', str(obj),
                   '-T', str(tools),
                   '-V', 'MKREPRO=yes', '-V', 'TOOLCHAIN_MISSING=yes',
                   '-V', 'MKREPRO_TIMESTAMP=1',
                   '-V', 'EMBER_COMPILER_FAMILY=clang',
                   '-V', 'EMBER_HOST_BFD_CONFIG=' + str(args.host_bfd_config.resolve(strict=True)),
                   '-V', 'EXTERNAL_TOOLCHAIN=' + str(prefix), 'show-params']
        result = subprocess.run(command, cwd=source, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        if result.returncode or ('NETBSDOBJDIR=' + str(obj) + '; export NETBSDOBJDIR') not in result.stdout:
            raise SystemExit('build.sh -O did not export the reproducible object root:\n' + result.stdout)
    print('build.sh reproducible object-root selection passed')


if __name__ == '__main__':
    main()
