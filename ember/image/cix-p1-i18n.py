#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Host-only inventory of native DATA; conversion tables are not generated here."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys

PREFIX = 'usr/share/i18n/'
REQUIRED = tuple(PREFIX + p for p in (
    'iconv/iconv.dir', 'esdb/esdb.dir', 'esdb/esdb.alias',
    'esdb/ISO646/ISO646-US.esdb', 'esdb/UTF/UTF-8.esdb',
    'csmapper/mapper.dir', 'csmapper/ISO646/ISO646-US%UCS.646'))
ALLOWED = frozenset(REQUIRED + tuple(PREFIX + p for p in (
    'esdb/esdb.dir.db', 'esdb/esdb.alias.db', 'csmapper/mapper.dir.db',
    'csmapper/charset.pivot', 'csmapper/charset.pivot.pvdb')))
INVENTORY = 'I18N-INVENTORY.json'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def regular(path):
    require(stat.S_ISREG(path.lstat().st_mode), f'Not a regular file: {path}')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON key')
        result[key] = value
    return result


def metadata(line, path, kind):
    words = line.split()
    require(bool(words) and words.pop(0) == './' + path, 'Invalid metadata path')
    patterns = {'uname': r'[A-Za-z_][A-Za-z0-9_-]*',
                'gname': r'[A-Za-z_][A-Za-z0-9_-]*',
                'mode': r'0?[0-7]{3,4}', 'size': r'[0-9]+',
                'uid': r'[0-9]+', 'gid': r'[0-9]+',
                'time': r'[0-9]+(?:\.[0-9]+)?', 'flags': r'[A-Za-z0-9_,+-]+',
                'sha256': r'[0-9a-f]{64}'}
    fields = {}
    for word in words:
        key, sep, value = word.partition('=')
        require(bool(sep) and key not in fields, 'Invalid metadata attribute')
        valid = value == kind if key == 'type' else (
            key in patterns and re.fullmatch(patterns[key], value) is not None)
        require(valid, 'Unsupported native DATA metadata')
        fields[key] = value
    require(all(k in fields for k in ('type', 'uname', 'gname', 'mode')),
            'Incomplete native DATA metadata')
    return fields


def walk_files(root):
    for directory, dirs, files in os.walk(root, followlinks=False):
        # Include symlink directories, which os.walk otherwise silently skips.
        for name in dirs + files:
            path = Path(directory) / name
            mode = path.lstat().st_mode
            if not stat.S_ISDIR(mode):
                yield path


def snapshot(cohort, revision):
    require(re.fullmatch(r'[0-9a-f]{40}', revision) is not None,
            'Invalid source revision')
    dest = cohort / 'dest'
    for part in ('usr', 'usr/share', 'usr/share/i18n'):
        require(stat.S_ISDIR((dest / part).lstat().st_mode),
                'DATA directory is not a directory')
    files = []
    for path in walk_files(dest / PREFIX):
        rel = path.relative_to(dest).as_posix()
        require(stat.S_ISREG(path.lstat().st_mode) and rel in ALLOWED,
                'Unsupported DATA path or file type')
        files.append(rel)
    require(set(REQUIRED) <= set(files), 'Required native DATA missing')
    paths = set(files)
    for path in files:
        parts = path.split('/')
        paths.update('/'.join(parts[:n]) for n in range(1, len(parts)))
    regular(dest / 'METALOG')
    native, data_native = {}, {}
    for line in (dest / 'METALOG').read_text().splitlines():
        words = line.split()
        key = words[0] if words else ''
        if key.startswith('./' + PREFIX):
            rel = key[2:]
            require(all(re.fullmatch(r'[A-Za-z0-9_%+-][A-Za-z0-9_.%+-]*', p)
                        for p in rel.split('/')), 'Malformed native DATA path')
            data_native[rel] = line.strip()
        if key.startswith('./') and key[2:] in paths:
            native[key[2:]] = line.strip()
    for path, line in data_native.items():
        kind = next((w[5:] for w in line.split() if w.startswith('type=')), None)
        require(kind in ('file', 'dir'), 'Unsupported native DATA type')
        metadata(line, path, kind)
        if kind == 'file':
            require(path in ALLOWED and path in files,
                    'Missing or unsupported native installed DATA')
    entries = []
    for path in sorted(paths):
        st = (dest / path).lstat()
        kind = 'dir' if stat.S_ISDIR(st.st_mode) else 'file'
        require(path in native, f'Missing native DATA metadata: {path}')
        line = native[path]
        fields = metadata(line, path, kind)
        row = {'path': path, 'metadata': line}
        if kind == 'file':
            row['sha256'] = digest(dest / path)
            require('size' not in fields or int(fields['size']) == st.st_size,
                    'Native metadata size mismatch')
            require('sha256' not in fields or fields['sha256'] == row['sha256'],
                    'Native metadata hash mismatch')
        entries.append(row)
    return {'format': 1, 'source': revision, 'entries': entries}


def verified(cohort, revision, receipt):
    path = cohort / INVENTORY
    regular(path)
    regular(receipt)
    pins = [line for line in receipt.read_text().splitlines(keepends=True)
            if line.startswith('i18n-inventory-sha256=')]
    require(pins == ['i18n-inventory-sha256=' + digest(path) + '\n'],
            'DATA receipt hash mismatch')
    stored = json.loads(path.read_text(), object_pairs_hook=unique_pairs)
    actual = snapshot(cohort, revision)
    require(json.dumps(stored, sort_keys=True) == json.dumps(actual, sort_keys=True),
            'Native DATA inventory differs')
    return actual


def copy(cohort, root, inventory):
    for row in inventory['entries']:
        path = root / row['path']
        kind = 'file' if 'sha256' in row else 'dir'
        fields = metadata(row['metadata'], row['path'], kind)
        if kind == 'dir':
            path.mkdir(parents=True, exist_ok=True)
        else:
            require(not path.exists() and not path.is_symlink(), 'Staged DATA already exists')
            shutil.copyfile(cohort / 'dest' / row['path'], path)
            path.chmod(int(fields['mode'], 8))
    for row in reversed(inventory['entries']):
        if 'sha256' not in row:
            fields = metadata(row['metadata'], row['path'], 'dir')
            (root / row['path']).chmod(int(fields['mode'], 8))


def check_copy(root, inventory):
    expected = sorted(row['path'] for row in inventory['entries'] if 'sha256' in row)
    actual = []
    require(stat.S_ISDIR((root / PREFIX).lstat().st_mode), 'Invalid staged DATA file type')
    for path in walk_files(root / PREFIX):
        require(stat.S_ISREG(path.lstat().st_mode), 'Invalid staged DATA file type')
        actual.append(path.relative_to(root).as_posix())
    require(sorted(actual) == expected, 'Staged DATA inventory differs')
    for row in inventory['entries']:
        path = root / row['path']
        mode = path.lstat().st_mode
        kind = 'file' if 'sha256' in row else 'dir'
        fields = metadata(row['metadata'], row['path'], kind)
        require((stat.S_ISREG(mode) if kind == 'file' else stat.S_ISDIR(mode)) and
                stat.S_IMODE(mode) == int(fields['mode'], 8), 'Changed staged DATA type/mode')
        if kind == 'file':
            require(digest(path) == row['sha256'], 'Changed staged DATA bytes')


def main(args):
    require((len(args) == 3 and args[0] == 'record') or
            (len(args) == 4 and args[0] == 'verify') or
            (len(args) == 5 and args[0] in ('copy', 'check-copy')),
            'Usage: cix-p1-i18n.py record COHORT REVISION | verify COHORT REVISION RECEIPT | copy/check-copy COHORT REVISION RECEIPT ROOT')
    action, cohort, revision = args[:3]
    cohort = Path(cohort)
    if action == 'record':
        data = snapshot(cohort, revision)
        fd = os.open(cohort / INVENTORY, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        with os.fdopen(fd, 'w') as output:
            json.dump(data, output, indent=2, ensure_ascii=False)
            output.write('\n')
    else:
        data = verified(cohort, revision, Path(args[3]))
        if action == 'copy':
            copy(cohort, Path(args[4]), data)
        elif action == 'check-copy':
            check_copy(Path(args[4]), data)
        else:
            for row in data['entries']:
                print(row['metadata'])


if __name__ == '__main__':
    try:
        main(sys.argv[1:])
    except (OSError, ValueError, KeyError, TypeError) as error:
        print('Native i18n DATA: ' + str(error), file=sys.stderr)
        sys.exit(1)
