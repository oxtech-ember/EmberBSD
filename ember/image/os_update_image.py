# SPDX-License-Identifier: BSD-2-Clause
"""Check the target programs and release anchor, and write the update image policy."""

import json
import os
from pathlib import Path
import re
import stat
import tempfile

SRC = Path(__file__).resolve().parents[2]


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


def atomic(path, data, mode, *, fat=False):
    path.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
    check(not path.is_symlink() and (not path.exists() or path.is_file()), f'unexpected entry: {path}')
    if path.exists() and path.read_bytes() == data and (fat or stat.S_IMODE(path.stat().st_mode) == mode):
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temp = Path(stream.name)
        try:
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


# Machine values from the ELF headers of supported FFS image targets.
TARGET_ELF = {arch: (bytes((2, 1)), machine.to_bytes(2, 'little'))
              for arch, machine in {'aarch64': 183, 'x86_64': 62}.items()}
ANCHOR = Path('etc/openssl/emberbsd-release-root.pem')


def check_program(root, name, arch):
    path = (root / name).resolve(strict=True)
    check(path.is_relative_to(root.resolve()) and os.access(path, os.X_OK)
          and elf_abi(path) == TARGET_ELF.get(arch), f'incompatible native target program: {name}')


def check_anchor(root):
    path = (root / ANCHOR).resolve(strict=True)
    check(path.is_relative_to(root.resolve())
          and path.read_bytes() == (SRC / 'etc/emberbsd-release-root.pem').read_bytes(),
          'release signing anchor differs from the OS trust input')


def image_policy(original):
    """Enable boot services when the site has not already set their policy."""
    for name in (b'certctl_init', b'ember_update'):
        if not re.search(rb'(?m)^\s*' + name + rb'\s*=', original):
            original = original.rstrip(b'\n') + b'\n' + name + b'=YES\n'
    return original
