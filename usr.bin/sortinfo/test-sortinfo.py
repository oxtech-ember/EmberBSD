#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check sorting of ordinary Info entries with continuation lines."""
import argparse
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sortinfo', required=True)
a = p.parse_args()
source = ('Info directory\n* Menu:\n\nSection Z\n'
          '* Zebra: (zebra).\n  Zebra continuation.\n'
          '* Alpha: (alpha).\n  Alpha continuation.\n'
          '\nSection A\n* Another: (other).\n')
expected = ('Info directory\n* Menu:\n\nSection A\n* Another: (other).\n'
            '\nSection Z\n* Alpha: (alpha).\n  Alpha continuation.\n'
            '* Zebra: (zebra).\n  Zebra continuation.\n')
result = subprocess.run([a.sortinfo], input=source, capture_output=True, text=True, check=True)
assert result.stdout == expected, result.stdout
print('sortinfo multiline menu entries: PASS')
