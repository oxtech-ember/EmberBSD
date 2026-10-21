# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; finite original U02 instruction fixture policy.
import argparse
import hashlib
import json
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
    parser.add_argument('--timeout', type=int, default=120)
    result = parser.parse_args(argv)
    if not all(value is None or value.is_absolute() for value in (result.firmware, result.output, result.compiler_prefix, result.sdk_source, result.qemu)) or result.timeout <= 0:
        raise ValueError('Absolute input/output paths and a positive timeout are required')
    if result.mode != 'prepare' and (result.compiler_prefix is None or result.sdk_source is None):
        raise ValueError('Build/run requires an explicit ARM GCC16 prefix and SDK API1 source')
    if result.mode == 'run' and result.qemu is None:
        raise ValueError('Run requires an explicit QEMU executable')
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


def prepare(original, work):
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
    fixtures = cases.inputs()
    (work / 'cases.inc').write_text(cases.emit(fixtures))
    json_rows = [dict(row, request=row['request'].hex(), frame=row['frame'].hex()) for row in fixtures]
    (work / 'cases.json').write_text(json.dumps(json_rows, indent=2) + '\n')
    (work / 'inputs.json').write_text(json.dumps(dict(original_sha256=ORIGINAL_SHA, original_size=ORIGINAL_SIZE, spans=rows, full_firmware_loaded=False, replacements=False), indent=2) + '\n')
    return rows


def build_commands(prefix, work, source):
    gcc = str(Path(prefix) / 'bin/arm-none-eabi-gcc')
    flags = ['-mthumb', '-mcpu=cortex-m4', '-mfloat-abi=soft', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-O2', '-Wall', '-Wextra', '-Werror']
    commands = []
    for name, path in [('harness', source / 'harness.c'), ('start', source / 'start.S'), ('original', work / 'original.S')]:
        commands.append([gcc] + flags + (['-std=c11'] if path.suffix == '.c' else []) + ['-I' + str(work), '-I' + str(source), '-c', str(path), '-o', str(work / (name + '.o'))])
    commands.append([gcc] + flags + ['-nostdlib', '-no-pie', '-Wl,--build-id=none', '-T', str(work / 'link.ld'), '-Wl,-Map=' + str(work / 'harness.map')] + [str(work / (name + '.o')) for name in ('start', 'harness', 'original')] + ['-o', str(work / 'harness.elf')])
    return commands
