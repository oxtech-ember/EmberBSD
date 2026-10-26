#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Test the actual host recipe selectors with an explicit SDK bmake tuple."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bmake', 'makefiles', 'shell', 'cp', 'rm', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    for path in (args.bmake, args.makefiles, args.shell, args.cp, args.rm, args.work):
        if not path.is_absolute():
            parser.error('Use absolute SDK paths')
    args.work.mkdir(parents=False, exist_ok=False)
    text = Path(__file__).with_name('Makefile').read_text()
    fixture = args.work / 'Makefile'
    fixture.write_text(text.split('.include', 1)[0] + '\nall:\n\t@:\n')
    base = [str(args.bmake), '-r', '-m', str(args.makefiles), '-f', str(fixture),
            'SHELL=' + str(args.shell), '-V', '${CPPFLAGS}']
    cases = [
        ('missing', [], False),
        ('relative', ['HOST_MV_CP=cp', 'HOST_MV_RM=' + str(args.rm)], False),
        ('absent', ['HOST_MV_CP=' + str(args.work / 'missing'),
                    'HOST_MV_RM=' + str(args.rm)], False),
        ('space', ['HOST_MV_CP=/some path/cp', 'HOST_MV_RM=' + str(args.rm)], False),
        ('shell', ['HOST_MV_CP=/some;echo', 'HOST_MV_RM=' + str(args.rm)], False),
        ('literal', ['HOST_MV_CP=' + str(args.cp), 'HOST_MV_RM=' + str(args.rm)], True)]
    records = []
    for name, selectors, success in cases:
        result = subprocess.run(base + selectors, cwd=args.work, env=os.environ,
                                capture_output=True, timeout=20)
        records.append(dict(case=name, argv=base + selectors, actual_exit=result.returncode,
                            stdout=result.stdout.decode(), stderr=result.stderr.decode()))
        (args.work / 'results.json').write_text(json.dumps(records, indent=2) + '\n')
        assert (result.returncode == 0) == success, records[-1]
        if success:
            assert shlex.split(result.stdout.decode()) == [
                '-DEMBERBSD_MV_CP="' + str(args.cp) + '"',
                '-DEMBERBSD_MV_RM="' + str(args.rm) + '"']
        else:
            assert result.stderr, records[-1]
    print('6 host child selector checks passed')
