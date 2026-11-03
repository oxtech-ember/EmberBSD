#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Remove the generated OpenSSL tree only from its owning object directory."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import shutil
import stat

parser = argparse.ArgumentParser()
parser.add_argument('--object-root', type=Path, required=True)
parser.add_argument('--source-root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--prepared-source', type=Path, required=True)
args = parser.parse_args()
root = args.object_root.resolve(strict=True)
source = args.source_root.resolve(strict=True)
output = args.output
prepared = args.prepared_source.resolve(strict=True)
if root == source or source in root.parents or root in source.parents:
    parser.error('object and source directories must not overlap')
if output.is_symlink() or output.parent.resolve(strict=True) != root or output.name != 'generated':
    parser.error('output must be the generated directory of the object root')
output = root / 'generated'
if output == prepared or prepared in output.parents or output in prepared.parents:
    parser.error('output and prepared source must not overlap')
lock = root / '.openssl-generation.lock'
fd = os.open(lock, os.O_CREAT | os.O_RDWR | getattr(os, 'O_NOFOLLOW', 0), 0o600)
if not stat.S_ISREG(os.fstat(fd).st_mode):
    parser.error('generation lock is not a regular file')
fcntl.flock(fd, fcntl.LOCK_EX)
if os.path.lexists(output):
    if output.is_symlink() or not output.is_dir():
        parser.error('output is not a real directory')
    marker = output / '.owned'
    if marker.is_symlink() or not marker.is_file():
        parser.error('output ownership marker is not a regular file')
    try:
        owner = json.loads(marker.read_text())
    except (OSError, ValueError):
        parser.error('output lacks its ownership marker')
    if owner != {'format': 'emberbsd-openssl-generated', 'object_root': str(root)}:
        parser.error('output ownership does not match object root')
    shutil.rmtree(output)
os.close(fd)
