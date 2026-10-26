#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# This file was developed with AI assistance.
"""Check the initial make-variable query with selected native executables."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('make', 'shell', 'echo'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    tools = {name: str(getattr(args, name).resolve(strict=True))
             for name in ('make', 'shell', 'echo')}
    contract = Path(__file__).with_name('contract').read_text().splitlines()[1:]
    with tempfile.TemporaryDirectory(prefix='ember-native-make-') as directory:
        routing = Path(directory)
        for row in contract:
            command, role, _ = row.split()
            if role in ('sh', 'echo'):
                (routing / command).symlink_to(tools['shell' if role == 'sh' else role])
        # NetBSD make executes this simple recipe directly. A shell builtin
        # cannot supply echo when its executable is absent from routed PATH.
        makefile = ('VALUE=yes\n_x_:\n\t@echo ${VALUE}\n'
                    '.SHELL: name=sh path=' + tools['shell'] + '\n')
        result = subprocess.run([tools['make'], '-r', '-f-', '_x_'],
                                input=makefile, text=True, capture_output=True,
                                cwd=directory, env={'PATH': directory}, timeout=30)
        if result.returncode != 0 or result.stdout != 'yes\n':
            raise SystemExit('native make variable query failed:\n' + result.stdout + result.stderr)
    print('native make variable query passed')


if __name__ == '__main__':
    main()
