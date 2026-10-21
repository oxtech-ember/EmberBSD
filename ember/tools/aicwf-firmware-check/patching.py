# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; finite consumer policy for reviewed own U02 patch sections.
import hashlib
import json
import re
from pathlib import Path
import struct

import policy

WINDOWS = (('key', '.aicwf_bip_key', 0x142be0, 340, 'b2712eebf753f9736e2dc26c513b77c9e12ee687a885099d1cf649671ba1294a'),
           ('rx', '.aicwf_bip_rx', 0x165e48, 384, '167300a59156fb0e39e41a7f0cdad5b3801f539899733b53f553332fcbe4fe4b'))
PROFILES = {
    '353667090df72253cadc2884f67a5446520e0c9a3b5616f2fd1d7e825fce74a3': ('bip-v1', '3752e698d695a682503f7555a0db5595ab8e41eaa1303cea3c2a6802b2c2b9f4', ('fd4e67a5ed57fc9e5fcdb917f24385eddd11655de336c262661379423722deca', '1e4e425c60c54449213e2c9907e52f602bc2856e52d0449bf08ea101d0267452')),
    '809f3ca375cb4d85eb6313fbdfe82c459edb81ca720cd29b8fe496188535f295': ('bip-v2', '1a9da748564928b255ed17d3b247bf89ea7d19a1cf1e319202c1e5b7c94c06a5', ('2aa62caf64c6b24570b48b22d9e574e8672ac105e2c2e2935936666f0dcaa023', '1e4e425c60c54449213e2c9907e52f602bc2856e52d0449bf08ea101d0267452')),
}
# A separate GCC16 controlled build must reproduce this explicit negative input.
MUTANT = {'3c5acaf6cd309afeddb9c8fb861634022989080318dc786d1c885c6c6c59cb8c': ('early-enable-causal', 'b7281bebe93f3ac4a7aa755017a8a7ac0791fdc835598ba7e24c1f73e1a055d3', ('4b7027793ad880970cf7c143630f9b51dccfdf75d6a0bfc4dfe0ae6f245b91d9', '14b1836c39f69cde66d38d09b4bcc46b4ec10aa29c15f0f62f096085aeeacda8'))}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_document(data):
    def unique(pairs):
        result = {}
        for name, value in pairs:
            if name in result:
                raise ValueError('Duplicate manifest field: ' + name)
            result[name] = value
        return result
    return json.loads(data, object_pairs_hook=unique)


def parse_elf(data):
    """Validate only the two-section ARM32 profile, not a general SDK ELF API."""
    if not 52 <= len(data) <= 65536 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected bounded ARM32 patch ELF')
    kind, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phcount, shsize, shcount, names_index = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    if (kind, machine, version, entry, flags, ehsize, phsize, phcount, shsize) != (2, 40, 1, 0x142be1, 0x5000200, 52, 32, 2, 40) or not 3 <= shcount <= 64 or not 0 < names_index < shcount:
        raise ValueError('Unsupported patch ELF header')
    def span(offset, count):
        if offset < 0 or count < 0 or offset + count > len(data):
            raise ValueError('Patch ELF range exceeds input')
        return data[offset:offset + count]
    if phoff < 52 or shoff < 52:
        raise ValueError('Patch ELF table overlaps header')
    span(phoff, phcount * 32); span(shoff, shcount * 40)
    headers = [struct.unpack_from('<10I', data, shoff + index * 40) for index in range(shcount)]
    if any(headers[0]):
        raise ValueError('Patch ELF null section differs')
    names_row = headers[names_index]
    if names_row[1] != 3 or names_row[5] > 4096:
        raise ValueError('Invalid patch section names')
    names = span(names_row[4], names_row[5])
    allocated, seen = {}, set()
    for row in headers[1:]:
        if row[0] >= len(names) or b'\0' not in names[row[0]:]:
            raise ValueError('Invalid patch section name')
        name = names[row[0]:names.index(b'\0', row[0])].decode('ascii')
        if name in seen or row[1] in (4, 9) and row[5]:
            raise ValueError('Duplicate section or unresolved patch relocation')
        seen.add(name)
        if row[1] != 8:
            span(row[4], row[5])
        if row[2] & 2:
            if row[1] != 1 or row[2] != 6 or row[8] != 4:
                raise ValueError('Unknown allocated patch section')
            allocated[name] = row
        elif name not in ('.symtab', '.strtab', '.shstrtab'):
            raise ValueError('Unknown nonallocated patch section')
    if set(allocated) != {row[1] for row in WINDOWS}:
        raise ValueError('Expected exactly two patch slots')
    loads = [struct.unpack_from('<8I', data, phoff + index * 32) for index in range(2)]
    result, intervals = {}, []
    for index, (key, name, address, size, unused_sha) in enumerate(WINDOWS):
        row, load = allocated[name], loads[index]
        if row[3] != address or row[5] != size or load[:7] != (1, row[4], address, address, size, size, 5):
            raise ValueError('Patch slot address, length or RX flags differ')
        result[key] = span(row[4], size)
        intervals.append((row[4], row[4] + size))
    if max(a for a, b in intervals) < min(b for a, b in intervals):
        raise ValueError('Patch file payloads overlap')
    return result


def load(elf, manifest, causal_early_enable=False):
    elf, manifest = Path(elf), Path(manifest)
    if any(not path.is_absolute() or not path.is_file() or path.is_symlink() for path in (elf, manifest)):
        raise ValueError('Absolute regular patch inputs are required')
    if elf.stat().st_size > 65536 or manifest.stat().st_size > 32768:
        raise ValueError('Patch input exceeds bound')
    data, metadata = elf.read_bytes(), manifest.read_bytes()
    known = MUTANT if causal_early_enable else PROFILES
    if digest(data) not in known:
        raise ValueError('Unknown patch ELF identity')
    profile, assembly_sha, expected = known[digest(data)]
    rows = parse_elf(data)
    value = json_document(metadata)
    fields = {'schema', 'state', 'original_sha256', 'original_size', 'firmware_base', 'inputs', 'tools', 'sdk_api', 'sdk_inputs', 'python_version', 'compiler_version', 'compiler_target', 'commands', 'sections', 'device_access', 'execution_accepted', 'loader_accepted'}
    if not isinstance(value, dict) or set(value) != fields or value['schema'] != 'aicwf-bip-patch-v1' or value['state'] != 'assembled-not-executed':
        raise ValueError('Unknown patch manifest schema or fields')
    if value['original_sha256'] != policy.ORIGINAL_SHA or type(value['original_size']) is not int or value['original_size'] != policy.ORIGINAL_SIZE or type(value['firmware_base']) is not int or value['firmware_base'] != policy.BASE:
        raise ValueError('Patch manifest original identity differs')
    if value['compiler_version'] != '16.2.0' or value['compiler_target'] != 'arm-none-eabi' or type(value['sdk_api']) is not int or value['sdk_api'] != 1 or any(value[name] is not False for name in ('device_access', 'execution_accepted', 'loader_accepted')):
        raise ValueError('Patch manifest build or acceptance boundary differs')
    if value['inputs'] != {'aicwf-firmware-bip.S': assembly_sha, 'aicwf-firmware-bip.ld': '000227df6210ffa6f25525c9bb04558b0d179cc238ebb2505b6a484f08186e46', 'aicwf-firmware-bip.py': '6933c7ac45f1582e6c98035b30d6efb190980c9df867d9754c5158e7768d9ddd'}:
        raise ValueError('Patch manifest source inputs differ')
    tool_hashes = {'gcc': '9e66537094e51d3b1f3a91d995ddea068763a7411735cd50e6fd8fa202ca2af4', 'as': 'cb670738055056c2356a60dc8b53226695849bda239a21b365afed5b56246a03', 'ld': '24a01a10b2ad483d570b01c75eff879f22f1f460d11c5c7998cbbf77f9e7296f', 'collect2': 'c5f7c0b67421ce49cea294ea27f34bdc8cd0017335fb653ebd488cdee637f6c3'}
    if not isinstance(value['tools'], dict) or set(value['tools']) != set(tool_hashes):
        raise ValueError('Patch manifest tool set differs')
    for name, expected_sha in tool_hashes.items():
        tool = value['tools'][name]
        if not isinstance(tool, dict) or set(tool) != {'path', 'sha256'} or tool['sha256'] != expected_sha or not isinstance(tool['path'], str) or not Path(tool['path']).is_absolute():
            raise ValueError('Patch manifest tool identity differs')
    modules = value['sdk_inputs']
    if not isinstance(modules, dict) or not {'__init__.py', 'process.py', 'paths.py', 'jsonio.py'} <= set(modules) or any(not isinstance(v, str) or re.fullmatch('[0-9a-f]{64}', v) is None for v in modules.values()):
        raise ValueError('Patch manifest SDK input identities differ')
    commands = value['commands']
    if not isinstance(commands, list) or len(commands) != 2 or any(not isinstance(argv, list) or not argv or any(not isinstance(v, str) or len(v) > 8192 or any(c in v for c in ('\0', '\n', '\r')) for v in argv) for argv in commands):
        raise ValueError('Patch manifest command metadata differs')
    if not isinstance(value['sections'], list) or len(value['sections']) != 2:
        raise ValueError('Patch manifest slot count differs')
    for index, (key, name, address, size, original_sha) in enumerate(WINDOWS):
        row = value['sections'][index]
        wanted = dict(section=name, address=address, size=size, original_sha256=original_sha, file=name[1:] + '.bin', replacement_sha256=expected[index])
        if row != wanted or type(row['address']) is not int or type(row['size']) is not int or digest(rows[key]) != expected[index]:
            raise ValueError('Patch manifest slot/hash differs')
    return dict(profile=profile, elf_sha256=digest(data), manifest_sha256=digest(metadata), sections=value['sections'], replacements=rows)


def record(value):
    return {name: data for name, data in value.items() if name != 'replacements'}
