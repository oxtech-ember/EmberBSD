# SPDX-License-Identifier: BSD-2-Clause
"""Install efiboot and its two slot menus in a fresh FAT staging tree."""

import re
import struct

from os_update_image import atomic, check

TARGETS = {
    'aarch64': ('bootaa64.efi', 0xaa64),
    'x86_64': ('bootx64.efi', 0x8664),
}


def initialize_efi(boot, loader, profile, *, indices, extra_args=()):
    """Return the marker directory for the common FFS image producer."""
    arch = profile['target']['arch']
    check(arch in TARGETS, 'unsupported EFI target')
    filename, machine = TARGETS[arch]
    check(not loader.is_symlink() and loader.is_file(), 'missing release EFI loader')
    data = loader.read_bytes()
    check(b'EFIBOOT_SLOT_TEST_ONLY' not in data, 'select the production EFI loader')
    check(len(data) >= 64 and data[:2] == b'MZ', 'invalid EFI DOS header')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    check(pe >= 64 and pe + 94 <= len(data) and data[pe:pe + 4] == b'PE\0\0' and
          struct.unpack_from('<H', data, pe + 4)[0] == machine and
          struct.unpack_from('<H', data, pe + 20)[0] >= 70 and
          struct.unpack_from('<H', data, pe + 24)[0] == 0x20b and
          struct.unpack_from('<H', data, pe + 92)[0] == 10,
          'select a PE32+ EFI application for the release architecture')
    check(set(indices) == {'A', 'B'} and
          all(type(value) is int and 1 <= value <= 16 for value in indices.values()) and
          indices['A'] != indices['B'], 'invalid EFI root partition numbers')
    check(isinstance(extra_args, (tuple, list)) and all(
          arg == '-v' or (arch == 'aarch64' and isinstance(arg, str) and
          re.fullmatch(r'console=[a-zA-Z0-9,._-]+', arg)) for arg in extra_args),
          'invalid efiboot kernel argument')
    provider = profile['reset_provider']
    check(isinstance(provider, str) and re.fullmatch(r'[a-z0-9][a-z0-9-]{0,30}', provider),
          'invalid EFI reset provider')
    roots = profile['roots']
    check(set(roots) == {'A', 'B'} and roots['A'] != roots['B'] and
          all(isinstance(label, str) and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,35}', label)
              for label in roots.values()), 'invalid EFI root labels')
    directory = boot / 'EFI' / 'EmberBSD'
    for path in (boot, boot / 'EFI', boot / 'EFI' / 'BOOT', directory):
        check(not path.is_symlink(), f'unexpected link in EFI stage: {path}')
    atomic(boot / 'EFI' / 'BOOT' / filename, data, 0o644, fat=True)
    atomic(directory / 'reset-provider', provider.encode('ascii'), 0o644, fat=True)
    for slot in ('A', 'B'):
        part = chr(ord('a') + indices[slot] - 1)
        args = list(extra_args)
        if arch == 'aarch64':
            args.append('root=NAME=' + roots[slot])
        command = ' '.join([f'boot bootpart{part}:netbsd', *args])
        config = f'menu=Boot EmberBSD slot {slot}:{command}\ndefault=1\ntimeout=0\n'
        atomic(directory / ('boot-' + slot.lower() + '.cfg'), config.encode('ascii'), 0o644, fat=True)
    return directory
