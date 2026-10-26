#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Test a selected native cp with independent Darwin metadata getters."""

import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import sys

CASES = ('file-replace', 'file-overlay', 'empty-acl-overlay', 'directory-inherit')


def run_case(fixture, cp, root, case):
    """Retain each command and its streams, including a required negative control."""
    if case not in CASES:
        raise ValueError('unknown metadata case')
    work = root / case
    work.mkdir()
    copy = [str(cp), '-Rp', 'src/.', 'dst'] if case == 'directory-inherit' else [str(cp), '-p', 'src', 'dst']
    commands = (
        ('prepare', [str(fixture), 'prepare', case], 0),
        ('before-copy', [str(fixture), 'observe', case], 1),
        ('copy', copy, 0),
        ('observe', [str(fixture), 'observe', case], 0),
    )
    environment = {key: os.environ[key] for key in
                   ('HOME', 'TMPDIR', 'ASAN_OPTIONS', 'UBSAN_OPTIONS') if key in os.environ}
    environment.update(LC_ALL='C', PATH=str(cp.parent))
    rows = []
    try:
        for name, argv, expected in commands:
            result = subprocess.run(argv, cwd=work, env=environment, stdin=subprocess.DEVNULL,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
            (root / (case + '-' + name + '.stdout')).write_bytes(result.stdout)
            (root / (case + '-' + name + '.stderr')).write_bytes(result.stderr)
            rows.append({'case': case, 'step': name, 'argv': argv, 'actual_exit': result.returncode})
            if result.returncode != expected or result.stdout or (expected == 0 and result.stderr):
                raise RuntimeError(f'{case}: {name} failed; see retained command streams')
        if sorted(path.name for path in work.iterdir()) != ['dst', 'src']:
            raise RuntimeError(f'{case}: unexpected filesystem entry')
        for name in ('src', 'dst'):
            path = work / name
            mode = path.lstat().st_mode
            if case == 'directory-inherit':
                if not stat.S_ISDIR(mode) or list(path.iterdir()):
                    raise RuntimeError(f'{case}: unexpected directory contents')
            elif not stat.S_ISREG(mode) or path.read_bytes() != b'source\n':
                raise RuntimeError(f'{case}: unexpected file contents')
    finally:
        (root / (case + '-commands.json')).write_text(json.dumps(rows, indent=2) + '\n')
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', required=True, type=Path, help='compiled filemeta-fixture executable')
    parser.add_argument('--cp', required=True, type=Path, help='selected native SDK cp executable')
    parser.add_argument('--work-root', required=True, type=Path, help='new directory for test files and logs')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('these native metadata fixtures require Darwin')
    fixture, cp = args.fixture.resolve(strict=True), args.cp.resolve(strict=True)
    for path in (fixture, cp):
        if not path.is_file() or not os.access(path, os.X_OK):
            parser.error(f'not an executable file: {path}')
    work = args.work_root.absolute()
    work.mkdir()
    for case in CASES:
        run_case(fixture, cp, work, case)
        print(f'{case}: PASS')


if __name__ == '__main__':
    main()
