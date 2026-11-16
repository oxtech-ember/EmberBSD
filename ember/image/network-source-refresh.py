# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Refresh selected prepared-source objects before a static network build."""
import json
import os
from pathlib import Path
import re
import shutil
import tempfile


STATE = 'NETWORK-OBJECT-SOURCES.json'


def _identities(value):
    if not isinstance(value, dict):
        raise ValueError('invalid network object source identities')
    for name, entry in value.items():
        if (not isinstance(name, str) or
                not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', name) or
                not isinstance(entry, dict) or set(entry) != {'record', 'path'} or
                not isinstance(entry['record'], str) or
                not re.fullmatch(r'[0-9a-f]{64}', entry['record']) or
                not isinstance(entry['path'], str) or
                not Path(entry['path']).is_absolute()):
            raise ValueError('invalid network object source identity')
    return value


def refresh(source, output, identities, crunch_owners, planner, clean_native,
            archive_members=None, allowed_source_roots=()):
    """Plan all cleanup first; commit object inputs before resumable compilation.

    The caller holds its output lease through cleanup, build and root commit.
    BUILD-INPUTS describes the last completed root, not the in-progress objects.
    Older root receipts cannot identify object inputs: the old producer could
    relink stale objects and stamp a new source receipt. The first use refreshes
    selected imported-source owners, retaining the foundation and other objects.
    """
    source, output = Path(source), Path(output)
    identities = _identities(identities)
    if output.is_symlink() or output.resolve() != output or not output.is_dir():
        raise ValueError('invalid network output root')
    state = output / STATE
    if state.is_symlink() or (state.exists() and not state.is_file()):
        raise ValueError('invalid network object source record')
    previous = {}
    if state.exists():
        record = json.loads(state.read_text())
        if (not isinstance(record, dict) or set(record) != {'version', 'sources'} or
                record['version'] != 1):
            raise ValueError('unsupported network object source record')
        previous = _identities(record['sources'])
    changed = sorted(name for name, identity in identities.items()
                     if previous.get(name) != identity)
    plans = []
    for directory, mapping in (('obj', None), ('crunch', crunch_owners)):
        objects = output / directory
        if objects.is_symlink() or (objects.exists() and not objects.is_dir()):
            raise ValueError('invalid network object directory')
        if not changed or not objects.exists():
            continue
        selected = planner.plan(source, objects, changed, archive_members,
            allowed_source_roots=allowed_source_roots,
            object_dir_map=mapping,
            # Crunchgen's top-level dispatch/stub/combined outputs have no
            # imported headers. Subdirectories retain normal dependency checks.
            extra_independent_patterns=(
                ('*.o', '*.cro', '*.ro', 'console-tools*') if mapping is not None else ()))
        plans.append((directory, objects, selected))
    for directory, objects, selected in plans:
        for name in selected:
            path = objects / name
            if path.is_symlink() or path.resolve() != path or not path.is_dir():
                raise ValueError('network refresh object changed after planning')
            if directory == 'obj':
                clean_native(name)
            else:
                # Only the mapped producer directory is owned here. Source
                # files and other crunch programs remain outside this scope.
                shutil.rmtree(path)
    updated = dict(previous)
    updated.update(identities)
    if not state.exists() or updated != previous:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode='w', dir=output,
                    prefix='.network-object-sources-', delete=False) as stream:
                temporary = stream.name
                json.dump({'version': 1, 'sources': updated}, stream, sort_keys=True)
                stream.write('\n')
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, state)
        finally:
            if temporary and os.path.exists(temporary):
                os.unlink(temporary)
    return changed
