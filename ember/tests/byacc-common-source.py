#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check both native and target byacc rules select the prepared source."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--bmake', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
for directory in ('external/bsd/byacc/bin', 'tools/yacc'):
    command = [str(a.bmake), '-m', str(root / 'share/mk'), '-C', str(root / directory),
               'NETBSDSRCDIR=' + str(root), '-V', '${IDIST}']
    missing = subprocess.run(command, capture_output=True, text=True)
    if missing.returncode == 0 or 'EMBER_BYACC_SOURCE' not in missing.stderr:
        raise SystemExit(directory + ': missing source was not rejected')
    for value in ('', 'relative/byacc', '/nonexistent/ember-byacc-source'):
        invalid = subprocess.run([*command, 'EMBER_BYACC_SOURCE=' + value],
                                 capture_output=True, text=True)
        if invalid.returncode == 0 or 'EMBER_BYACC_SOURCE' not in invalid.stderr:
            raise SystemExit(directory + ': invalid source was not rejected: ' + value)
    result = subprocess.run([*command, 'EMBER_BYACC_SOURCE=' + str(a.source)],
                            capture_output=True, text=True, check=True)
    if result.stdout.strip() != str(a.source):
        raise SystemExit(directory + ': selected source differs')
print('host and target byacc source selection: PASS')
