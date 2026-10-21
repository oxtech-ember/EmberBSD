# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; finite original U02 instruction fixture policy.
import argparse
import hashlib
import json
import struct
from pathlib import Path

ORIGINAL_SHA = 'd2c7b7ac0bf8682c976c0c2da34f204c9b2f1ef8821252c6348a3016bd638f24'
ORIGINAL_SIZE = 341200
BASE = 0x120000
SPANS = {
    'fill': (0x1204cc, 0x12056c),
    'key': (0x142be0, 0x142d34),
    'finder': (0x1541e4, 0x154224),
    'classifier': (0x165db4, 0x165e48),
    'rx': (0x165e48, 0x165fc8),
    'aes': (0x166104, 0x16651c),
    'cmac': (0x16651c, 0x1667e0),
    'tables': (0x1667e0, 0x166ae0),
    'copy': (0x16e4e0, 0x16e614),
}


def arguments(argv):
    seen = set()
    for arg in argv:
        if arg.startswith('--'):
            name = arg.split('=', 1)[0]
            if name in seen:
                raise ValueError('Repeated option: ' + name)
            seen.add(name)
    parser = argparse.ArgumentParser(description='Check selected original U02 instructions. This is not a full firmware boot.')
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--mode', choices=('prepare', 'build', 'run'), default='prepare')
    parser.add_argument('--compiler-prefix', type=Path)
    parser.add_argument('--qemu', type=Path)
    parser.add_argument('--sdk-source', type=Path)
    parser.add_argument('--patch-elf', type=Path)
    parser.add_argument('--patch-manifest', type=Path)
    parser.add_argument('--exercise', choices=('functional', 'irq'), default='functional')
    parser.add_argument('--causal-early-enable', action='store_true')
    parser.add_argument('--timeout', type=int, default=120)
    result = parser.parse_args(argv)
    if not all(value is None or value.is_absolute() for value in (result.firmware, result.output, result.compiler_prefix, result.sdk_source, result.qemu, result.patch_elf, result.patch_manifest)) or result.timeout <= 0:
        raise ValueError('Absolute input/output paths and a positive timeout are required')
    if result.mode != 'prepare' and (result.compiler_prefix is None or result.sdk_source is None):
        raise ValueError('Build/run requires an explicit ARM GCC16 prefix and SDK API1 source')
    if result.mode == 'run' and result.qemu is None:
        raise ValueError('Run requires an explicit QEMU executable')
    if (result.patch_elf is None) != (result.patch_manifest is None):
        raise ValueError('Both patch ELF and manifest are required')
    if result.exercise == 'irq' and result.patch_elf is None or result.causal_early_enable and result.exercise != 'irq':
        raise ValueError('IRQ and causal modes require explicit patch inputs')
    return result


def layout():
    rows = [(start, end, '.original.' + name) for name, (start, end) in SPANS.items()]
    rows += [(0x153d84, 0x153d88, '.stub.rng'), (0x156520, 0x156524, '.stub.assert')]
    result, cursor = [], 0x10000
    for start, end, name in sorted(rows):
        if start < cursor or start % 2 or end % 2:
            raise ValueError('Overlapping or unaligned instruction placement')
        if cursor != start:
            result.append((cursor, start, '.trap.' + str(len(result))))
        result.append((start, end, name))
        cursor = end
    result.append((cursor, 0x180000, '.trap.last'))
    return result


def prepare(original, work, patch=None, fixtures=None):
    original, work = Path(original), Path(work)
    data = original.read_bytes()
    if len(data) != ORIGINAL_SIZE or hashlib.sha256(data).hexdigest() != ORIGINAL_SHA:
        raise ValueError('Unknown original U02 identity')
    if work.exists() or work.is_symlink():
        raise ValueError('Output already exists')
    work.mkdir(mode=0o700)
    rows = []
    assembly = []
    for name, (start, end) in SPANS.items():
        selected = data[start - BASE:end - BASE]
        if patch is not None and name in patch['replacements']:
            selected = patch['replacements'][name]
        path = work / (name + '.bin')
        path.write_bytes(selected)
        rows.append(dict(name=name, start=start, end=end, bytes=len(selected), sha256=hashlib.sha256(selected).hexdigest()))
        assembly += ['.section .original.' + name + ',"a",%progbits', '.incbin "' + name + '.bin"']
    sections = []
    for start, end, name in layout():
        if name.startswith('.trap.'):
            assembly += ['.section ' + name + ',"ax",%progbits', '.fill ' + str((end - start) // 2) + ',2,0xde00']
        sections.append(' ' + name + ' ' + hex(start) + ' : { KEEP(*(' + name + ')) }')
        sections.append(' ASSERT(SIZEOF(' + name + ') == ' + str(end - start) + ', "Instruction span size mismatch")')
    (work / 'original.S').write_text('\n'.join(assembly) + '\n')
    template = (Path(__file__).resolve().parent / 'link.ld.in').read_text()
    (work / 'link.ld').write_text(template.replace('@ORIGINAL_SECTIONS@', '\n'.join(sections)))
    import cases
    fixtures = cases.inputs() if fixtures is None else fixtures
    (work / 'cases.inc').write_text(cases.emit(fixtures))
    json_rows = [dict(row, request=row['request'].hex(), frame=row['frame'].hex()) for row in fixtures]
    (work / 'cases.json').write_text(json.dumps(json_rows, indent=2) + '\n')
    if patch is not None:
        import patching
        (work / 'patch-inputs.json').write_text(json.dumps(patching.record(patch), indent=2) + '\n')
    (work / 'inputs.json').write_text(json.dumps(dict(original_sha256=ORIGINAL_SHA, original_size=ORIGINAL_SIZE, spans=rows, full_firmware_loaded=False, replacements=patch is not None), indent=2) + '\n')
    return rows


def build_commands(prefix, work, source):
    gcc = str(Path(prefix) / 'bin/arm-none-eabi-gcc')
    flags = ['-mthumb', '-mcpu=cortex-m4', '-mfloat-abi=soft', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-O2', '-Wall', '-Wextra', '-Werror']
    commands = []
    for name, path in [('harness', source / 'harness.c'), ('start', source / 'start.S'), ('original', work / 'original.S')]:
        commands.append([gcc] + flags + (['-std=c11'] if path.suffix == '.c' else []) + ['-I' + str(work), '-I' + str(source), '-c', str(path), '-o', str(work / (name + '.o'))])
    commands.append([gcc] + flags + ['-nostdlib', '-no-pie', '-Wl,--build-id=none', '-T', str(work / 'link.ld'), '-Wl,-Map=' + str(work / 'harness.map')] + [str(work / (name + '.o')) for name in ('start', 'harness', 'original')] + ['-o', str(work / 'harness.elf')])
    return commands


def elf_layout(path):
    """Reject misplaced fixture segments before any CPU execution."""
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected a little-endian ELF32 fixture')
    fields = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    kind, machine, version, entry, phoff, _, flags, ehsize, phsize, phcount, _, _, _ = fields
    if (kind, machine, version, flags, ehsize, phsize) != (2, 40, 1, 0x5000200, 52, 32) or not 0x401 <= entry < 0x10000 or not entry & 1:
        raise ValueError('Unexpected ARM soft-float fixture header')
    if phoff < 52 or phoff + phcount * phsize > len(data):
        raise ValueError('Truncated fixture program headers')
    loads = []
    for index in range(phcount):
        kind, offset, vma, physical, size, memory, access, alignment = struct.unpack_from('<8I', data, phoff + index * phsize)
        if kind != 1:
            continue
        if physical != vma or size > memory or offset + size > len(data) or memory == 0:
            raise ValueError('Invalid fixture load segment')
        loads.append(dict(start=vma, end=vma + memory, file_bytes=size, flags=access))
    loads.sort(key=lambda row: row['start'])
    if len(loads) != 3 or not (
        loads[0]['start'] == 0 and loads[0]['end'] <= 0x10000 and loads[0]['flags'] == 5 and
        loads[1]['start'] == 0x10000 and loads[1]['end'] == 0x180000 and loads[1]['flags'] == 5 and
        loads[2]['start'] == 0x20000000 and loads[2]['end'] < 0x20300000 and loads[2]['flags'] == 6
    ):
        raise ValueError('Fixture load segments overlap original globals or leave declared RAM')
    return dict(elf_sha256=hashlib.sha256(data).hexdigest(), entry=entry, loads=loads)
