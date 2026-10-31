#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Remove only a Waf output directory owned by the selected make objdir."""
import argparse
import os
from pathlib import Path
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--objdir', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
root, output = Path(args.objdir), Path(args.output)
if not root.is_absolute() or not output.is_absolute():
    parser.error('objdir and output must be absolute')
if '..' in root.parts or '..' in output.parts:
    parser.error('objdir and output must not contain parent components')
root = Path(os.path.normpath(root))
output = Path(os.path.normpath(output))
if root.resolve() != root or not root.is_dir():
    parser.error('objdir must be a real directory')
try:
    relative = output.relative_to(root)
except ValueError:
    parser.error('output must be inside objdir')
if not relative.parts:
    parser.error('output must not be objdir')
current = root
for part in relative.parts:
    current = current / part
    if current.is_symlink():
        parser.error('output path must not contain symlinks')
if output.exists():
    if not output.is_dir():
        parser.error('output must be a directory')
    shutil.rmtree(output)
