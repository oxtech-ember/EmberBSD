# SPDX-License-Identifier: BSD-2-Clause
"""Install the common rc service, release receipt and trusted native client configuration."""

import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

SRC = Path(__file__).resolve().parents[2]
STATE = Path('var/db/ember-os-update')
SERVICE = Path('etc/rc.conf.d/ember_os_update')
BACKEND = Path('usr/libexec/ember-os-slot')
HEALTH = Path('etc/ember-os-health')


def check(test, message):
    if not test:
        raise ValueError(message)


def encoded(value):
    return json.dumps(value, sort_keys=True, indent=2).encode() + b'\n'


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def atomic(path, data, mode, *, fat=False, owner=None):
    path.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
    check(not path.is_symlink() and (not path.exists() or path.is_file()), f'unexpected entry: {path}')
    if path.exists() and path.read_bytes() == data and (fat or stat.S_IMODE(path.stat().st_mode) == mode):
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temp = Path(stream.name)
        try:
            if owner is not None:
                os.fchown(stream.fileno(), *owner)
            if not fat:
                os.fchmod(stream.fileno(), mode)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
            os.replace(temp, path)
            sync_directory(path.parent)
        finally:
            temp.unlink(missing_ok=True)


def elf_abi(path):
    with path.open('rb') as stream:
        head = stream.read(20)
    check(len(head) == 20 and head[:4] == b'\x7fELF' and head[4] in (1, 2)
          and head[5] in (1, 2) and head[18:20] != b'\0\0', f'invalid ELF header: {path}')
    return head[4:6], head[18:20]


def check_runtime(root, target, kernel):
    client = root / 'usr/local/libexec/ember-sdk.pyz'
    python = root / 'usr/pkg/bin/python3.15'
    check(elf_abi(python) == elf_abi(kernel), 'native Python and installed kernel have different ELF targets')
    repos = json.loads((root / 'usr/local/etc/ember-sdk.json').read_bytes())['repositories'].values()
    native = [r.get('target') for r in repos if r.get('install_mode') == 'native']
    check(native == [target], 'native package target differs from the release profile')
    help_text = subprocess.run([sys.executable, '-I', client, '--help'], check=True, capture_output=True)
    check(b'fetch-os-update' in help_text.stdout, 'install a native SDK client with fetch-os-update')


def service_files(root, profile, commit, archive, digest, backend, health):
    check(re.fullmatch('[0-9a-f]{40}', commit), 'invalid OS commit')
    original = archive.read_bytes()
    check(hashlib.sha256(original).hexdigest() == digest, 'initial etc archive SHA-256 mismatch')
    head = dict(profile=profile, generation=0, os_commit=commit)
    receipt = root / STATE / 'release.json'
    check(not receipt.exists() or json.loads(receipt.read_bytes()) == head,
          'another OS generation is already installed')
    check(not (root / STATE / 'attempt.json').exists(), 'an update has already used this installation')
    control = dict(profile=profile, backend='/' + str(BACKEND), health='/' + str(HEALTH))
    programs = {
        Path('usr/libexec/ember-os-update'): (SRC / 'libexec/ember-os-update/ember-os-update').read_bytes(),
        Path('etc/rc.d/ember_os_update'): (SRC / 'etc/rc.d/ember_os_update').read_bytes(),
        BACKEND: backend.read_bytes(),
        HEALTH: health.read_bytes(),
    }
    configs = {
        Path('etc/ember-os-update.json'): encoded(control),
        STATE / 'etc.tar.xz': original,
        STATE / 'release.json': encoded(head),
    }
    return programs, configs
