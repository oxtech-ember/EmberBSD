#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Snapshot selected Waf configuration inputs for incremental rebuilds."""
import argparse
import hashlib
import os
from pathlib import Path
import stat

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', required=True)
p.add_argument('inputs', nargs='+')
a = p.parse_args()
root = Path(a.source).resolve(strict=True)
digest = hashlib.sha256()

def add(path):
    relative = path.relative_to(root).as_posix().encode()
    mode = path.lstat().st_mode
    if stat.S_ISLNK(mode):
        kind, data = b'L', os.readlink(path).encode()
    elif stat.S_ISREG(mode):
        kind, data = b'F', path.read_bytes()
    elif stat.S_ISDIR(mode):
        kind, data = b'D', b''
    else:
        raise ValueError('unsupported Waf configuration input: ' + str(path))
    for part in (relative, kind, data):
        digest.update(len(part).to_bytes(8, 'big'))
        digest.update(part)
    if stat.S_ISDIR(mode):
        for child in sorted(path.iterdir(), key=lambda item: item.name):
            add(child)

for item in a.inputs:
    path = root / item
    if Path(item).is_absolute() or '..' in Path(item).parts:
        p.error('configuration inputs must be relative to the source')
    add(path)
print(digest.hexdigest())
