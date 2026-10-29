#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Exercise temporary mode changes without following symlinks.
import importlib.util
import os
from pathlib import Path
import stat
import tempfile

spec = importlib.util.spec_from_file_location('modes', Path(__file__).with_name('target-acceptance-modes.py'))
modes = importlib.util.module_from_spec(spec)
spec.loader.exec_module(modes)

with tempfile.TemporaryDirectory() as temporary:
    base = Path(temporary)
    root = base / 'root'
    root.mkdir()
    directory = root / 'closed dir'
    directory.mkdir()
    file = directory / 'private'
    file.write_text('data')
    os.link(file, root / 'alias')
    outside = base / 'outside'
    outside.write_text('outside')
    outside.chmod(0)
    (root / 'link').symlink_to(outside)
    file.chmod(0)
    directory.chmod(0o111)
    state, mtree = base / 'state', base / 'spec'
    state.touch()
    mtree.touch()
    modes.relax(root, state)
    modes.append_modes(state, mtree)
    text = mtree.read_text()
    assert './closed\\040dir type=dir mode=0111' in text
    assert './closed\\040dir/private type=file mode=0000' in text
    assert './alias type=file mode=0000' in text
    assert stat.S_IMODE(outside.stat().st_mode) == 0
    modes.restore(root, state)
    assert stat.S_IMODE(directory.stat().st_mode) == 0o111
    assert stat.S_IMODE(file.stat().st_mode) == 0
    directory.chmod(0o700)
    file.chmod(0o600)
    outside.chmod(0o600)
    directory.chmod(0o111)
    original = modes.os.scandir
    def fail(path):
        if path == directory:
            raise OSError('fixture traversal failure')
        return original(path)
    modes.os.scandir = fail
    try:
        try:
            modes.relax(root, state)
            raise AssertionError('expected traversal failure')
        except OSError:
            pass
        modes.restore(root, state)
        assert stat.S_IMODE(directory.stat().st_mode) == 0o111
    finally:
        modes.os.scandir = original
        directory.chmod(0o700)
print('PASS: mode restoration, aliases, escaping, symlinks and partial failure')
