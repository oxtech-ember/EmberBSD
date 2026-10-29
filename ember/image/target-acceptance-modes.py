#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Temporarily open an owned image tree while retaining its target modes."""
import json
import os
from pathlib import Path
import stat
import sys


def records(path):
    with open(path, encoding='utf-8') as stream:
        return [json.loads(line) for line in stream]


def relax(root, state):
    # Record each change before chmod, so even a partial traversal can unwind.
    with open(state, 'w', encoding='utf-8') as output:
        seen = {}
        def visit(path):
            st = path.lstat()
            mode = stat.S_IMODE(st.st_mode)
            if stat.S_ISDIR(st.st_mode):
                kind, needed = 'dir', stat.S_IRUSR | stat.S_IXUSR
            elif stat.S_ISREG(st.st_mode):
                kind, needed = 'file', stat.S_IRUSR
            else:
                return
            key = (st.st_dev, st.st_ino)
            original = seen.get(key, mode)
            changed = (original & needed) != needed
            if changed:
                seen[key] = original
                output.write(json.dumps([os.fspath(path.relative_to(root)), kind, original], ensure_ascii=True) + '\n')
                output.flush()
                if mode != (mode | needed):
                    os.chmod(path, mode | needed, follow_symlinks=False)
            if kind == 'dir':
                with os.scandir(path) as entries:
                    children = sorted((Path(e.path) for e in entries), key=os.fsencode)
                for child in children:
                    visit(child)
        visit(root)


def mtree_path(path):
    raw = os.fsencode(path)
    safe = b'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/._-+'
    return ''.join(chr(b) if b in safe else '\\%03o' % b for b in raw)


def append_modes(state, spec):
    with open(spec, 'a', encoding='ascii') as output:
        for name, kind, mode in records(state):
            path = '.' if name == '.' else './' + mtree_path(name)
            output.write(f'{path} type={kind} mode={mode:04o}\n')


def restore(root, state):
    for name, _, mode in reversed(records(state)):
        os.chmod(root / name, mode, follow_symlinks=False)


if __name__ == '__main__':
    if len(sys.argv) != 4 or sys.argv[1] not in ('relax', 'append', 'restore'):
        raise SystemExit('usage: target-acceptance-modes.py relax|restore ROOT STATE | append STATE SPEC')
    action, a, b = sys.argv[1:]
    if action == 'relax':
        relax(Path(a), Path(b))
    elif action == 'append':
        append_modes(Path(a), Path(b))
    else:
        restore(Path(a), Path(b))
