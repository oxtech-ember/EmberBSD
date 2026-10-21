#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: Independently authored, AI-assisted fixed-image interoperability tool.
"""Build two bounded U02 replacement sections. Do not load or run firmware."""

import argparse
import hashlib
import importlib
import os
from pathlib import Path
import signal
import struct
import sys

ORIGINAL_SHA256 = 'd2c7b7ac0bf8682c976c0c2da34f204c9b2f1ef8821252c6348a3016bd638f24'
ORIGINAL_SIZE = 341200
ORIGINAL_BASE = 0x120000
WINDOWS = (
    ('.aicwf_bip_key', 0x142be0, 340,
     'b2712eebf753f9736e2dc26c513b77c9e12ee687a885099d1cf649671ba1294a'),
    ('.aicwf_bip_rx', 0x165e48, 384,
     '167300a59156fb0e39e41a7f0cdad5b3801f539899733b53f553332fcbe4fe4b'),
)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def check_original(data):
    if len(data) != ORIGINAL_SIZE or sha256(data) != ORIGINAL_SHA256:
        raise ValueError('unsupported original U02 image')
    for unused_name, address, size, expected in WINDOWS:
        offset = address - ORIGINAL_BASE
        if sha256(data[offset:offset + size]) != expected:
            raise ValueError('original replacement span differs')


def patch_sections(data):
    """Read this producer's finite ELF32 ARM profile. SDK ELF APIs are ELF64."""
    if (not 52 <= len(data) <= 65536 or
            data[:7] != b'\x7fELF\x01\x01\x01'):
        raise ValueError('expected bounded ELF32 little-endian input')
    (kind, machine, version, entry, phoff, shoff, flags, ehsize,
     phsize, phcount, shsize, shcount, names_index) = struct.unpack_from(
         '<HHIIIIIHHHHHH', data, 16)
    if (kind != 2 or machine != 40 or version != 1 or entry != 0x142be1 or
            ehsize != 52 or flags & 0xff000000 != 0x05000000 or
            phsize != 32 or phcount != 2 or shsize != 40 or
            not 3 <= shcount <= 64 or not 0 < names_index < shcount):
        raise ValueError('unsupported linked ARM patch profile')

    def span(offset, length):
        if offset < 0 or length < 0 or offset > len(data) or length > len(data) - offset:
            raise ValueError('ELF data outside file bounds')
        return data[offset:offset + length]

    if phoff < 52 or shoff < 52:
        raise ValueError('ELF table overlaps its header')
    span(phoff, phcount * phsize)
    span(shoff, shcount * shsize)
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * 40)
                for i in range(shcount)]
    names_row = sections[names_index]
    if names_row[1] != 3 or names_row[5] > 4096:
        raise ValueError('invalid section-name table')
    names = span(names_row[4], names_row[5])
    allocated = {}
    all_names = set()
    for row in sections[1:]:
        name_offset, section_type, section_flags, address, offset, size = row[:6]
        if name_offset >= len(names) or b'\0' not in names[name_offset:]:
            raise ValueError('invalid section name')
        try:
            name = names[name_offset:names.index(b'\0', name_offset)].decode('ascii')
        except UnicodeError as exc:
            raise ValueError('non-ASCII section name') from exc
        if name in all_names:
            raise ValueError('duplicate section name')
        all_names.add(name)
        if section_type in (4, 9) and size:
            raise ValueError('unresolved ELF relocations')
        if section_type != 8:
            span(offset, size)
        if section_flags & 2:
            if section_flags != 6 or section_type != 1 or row[8] != 4:
                raise ValueError('unexpected allocated section type or flags')
            allocated[name] = row
    if set(allocated) != {row[0] for row in WINDOWS}:
        raise ValueError('expected exactly two allocated replacement sections')
    loads = set()
    for index in range(phcount):
        row = struct.unpack_from('<IIIIIIII', data, phoff + index * phsize)
        if row[0] != 1 or row[6] != 5 or row[4] != row[5]:
            raise ValueError('unexpected load segment')
        loads.add((row[1], row[2], row[3], row[4]))
    result, file_spans = [], []
    for name, address, size, original_sha in WINDOWS:
        row = allocated[name]
        if (row[3] != address or row[5] != size or
                (row[4], address, address, size) not in loads):
            raise ValueError('replacement window differs from its fixed profile')
        file_spans.append((row[4], row[4] + size))
        result.append(dict(section=name, address=address, size=size,
                           original_sha256=original_sha,
                           file=name[1:] + '.bin', data=span(row[4], size)))
    if max(file_spans[0][0], file_spans[1][0]) < min(file_spans[0][1], file_spans[1][1]):
        raise ValueError('replacement file spans overlap')
    return result



def kernel_header(rows, modified_sha256, inputs):
    """Emit only independently authored bytes and public input digests."""
    lines = ['/* SPDX-License-Identifier: BSD-2-Clause */',
             '/* Generated by aicwf-firmware-bip.py. Do not edit. */',
             '/* AI-assisted interoperability code; no vendor payload. */']
    for name, digest in sorted(inputs.items()):
        lines.append('/* ' + name + ' SHA256 ' + digest + ' */')
    lines += ['#ifndef _DEV_SDMMC_IF_AICWF_FW_BIP_DATA_H_',
              '#define _DEV_SDMMC_IF_AICWF_FW_BIP_DATA_H_', '']
    values = [('original_sha256', bytes.fromhex(ORIGINAL_SHA256)),
              ('modified_sha256', bytes.fromhex(modified_sha256))]
    for row, window in zip(rows, WINDOWS, strict=True):
        if (row['section'], row['address'], len(row['data'])) != window[:3]:
            raise ValueError('unsupported kernel-header window')
        values.append((row['section'].removeprefix('.aicwf_bip_'), row['data']))
    for name, data in values:
        lines.append('static const uint8_t aicwf_fw_bip_' + name +
                     '[' + str(len(data)) + '] = {')
        for offset in range(0, len(data), 12):
            lines.append('\t' + ', '.join('0x%02x' % value for value in
                                         data[offset:offset + 12]) + ',')
        lines += ['};', '']
    lines += ['#endif', '']
    return '\n'.join(lines).encode('ascii')


def sdk_modules(root):
    source = Path(root).resolve(strict=True) / 'src'
    sys.path.insert(0, str(source))
    package = importlib.import_module('emberbsd_sdk')
    if package.API_VERSION != 1:
        raise ValueError('unsupported SDK host API')
    result = {}
    for name, required in (
            ('paths', ('canonical_existing', 'canonical_new', 'reject_overlap',
                       'open_regular', 'verify_stable', 'regular_file')),
            ('jsonio', ('write_json',)),
            ('process', ('capture', 'run_logged', 'sanitized_environment',
                         'SessionInspector'))):
        module = importlib.import_module('emberbsd_sdk.' + name)
        if not Path(module.__file__).resolve().is_relative_to(source):
            raise ValueError('SDK module does not belong to the selected source')
        if any(not callable(getattr(module, item, None)) for item in required):
            raise ValueError('SDK host capability is absent: ' + name)
        result[name] = module
    return result


def build(args):
    sdk = sdk_modules(args.sdk_source)
    paths, process, jsonio = sdk['paths'], sdk['process'], sdk['jsonio']

    def read(path, limit=64 * 1024 * 1024, exact=None):
        handle, before = paths.open_regular(path)
        with handle:
            if before.st_size > limit or (exact is not None and before.st_size != exact):
                raise ValueError('input file exceeds its fixed size profile')
            data = handle.read(limit + 1)
        paths.verify_stable(path, before)
        return data

    original = paths.canonical_existing(args.original)
    original_bytes = read(original, limit=ORIGINAL_SIZE, exact=ORIGINAL_SIZE)
    check_original(original_bytes)
    sources = Path(__file__).resolve().parent
    inputs = {name: sha256(read(sources / name)) for name in (
        'aicwf-firmware-bip.S', 'aicwf-firmware-bip.ld', 'aicwf-firmware-bip.py')}
    toolchain = paths.canonical_existing(args.toolchain)
    gcc = paths.canonical_existing(Path(toolchain) / 'bin/arm-none-eabi-gcc')
    environment = process.sanitized_environment()
    environment['PATH'] = str(Path(toolchain) / 'bin') + os.pathsep + os.defpath
    compiler_version = process.capture([gcc, '-dumpfullversion'], env=environment).stdout.decode('ascii').strip()
    machine = process.capture([gcc, '-dumpmachine'], env=environment).stdout.strip()
    if compiler_version.split('.')[0] != '16' or machine != b'arm-none-eabi':
        raise ValueError('expected the selected GCC16 ARM bare-metal compiler')
    tools = {'gcc': dict(path=gcc, sha256=sha256(read(gcc)))}
    for name in ('as', 'ld', 'collect2'):
        value = process.capture([gcc, '-print-prog-name=' + name], env=environment).stdout.decode().strip()
        resolved = paths.canonical_existing(value)
        if not Path(resolved).is_relative_to(toolchain):
            raise ValueError('compiler tool is outside the selected toolchain')
        tools[name] = dict(path=resolved, sha256=sha256(read(resolved)))
    sdk_package = Path(args.sdk_source).resolve(strict=True) / 'src/emberbsd_sdk'
    sdk_inputs = {item.name: sha256(read(item)) for item in sorted(sdk_package.glob('*.py'))}
    output = Path(paths.canonical_new(args.output))
    for source in (sources.parent.parent, args.sdk_source, original, toolchain):
        paths.reject_overlap(output, source)
    # Prove the process backend before creating output or starting GCC.
    process.SessionInspector(env=environment)
    output.mkdir(mode=0o700)
    temporary = output / 'tmp'
    temporary.mkdir(mode=0o700)
    environment['TMPDIR'] = str(temporary)
    pending = output / 'incomplete.json'
    jsonio.write_json(pending, dict(state='build-pending', inputs=inputs), mode=0o600)
    commands = (
        [gcc, '-v', '-mcpu=cortex-m4', '-mthumb', '-c',
         str(sources / 'aicwf-firmware-bip.S'), '-o', str(output / 'bip.o')],
        [gcc, '-v', '-mcpu=cortex-m4', '-mthumb', '-nostdlib',
         '-Wl,--fatal-warnings', '-Wl,--build-id=none',
         '-Wl,-T,' + str(sources / 'aicwf-firmware-bip.ld'),
         '-Wl,-Map,' + str(output / 'bip.map'), str(output / 'bip.o'),
         '-o', str(output / 'bip.elf')],
    )
    for index, argv in enumerate(commands):
        process.run_logged(argv, output / ('command-' + str(index) + '.log'),
                           cwd=str(output), env=environment)
    rows = patch_sections(read(output / 'bip.elf', limit=65536))
    modified = bytearray(original_bytes)
    for row in rows:
        offset = row['address'] - ORIGINAL_BASE
        modified[offset:offset + row['size']] = row['data']
    modified_sha256 = sha256(modified)
    header = kernel_header(rows, modified_sha256, inputs)
    with (output / 'if_aicwf_fw_bip_data.h').open('xb') as handle:
        handle.write(header)
        handle.flush()
        os.fsync(handle.fileno())
    for row in rows:
        data = row.pop('data')
        row['replacement_sha256'] = sha256(data)
        with (output / row['file']).open('xb') as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
    if (sha256(read(original, limit=ORIGINAL_SIZE, exact=ORIGINAL_SIZE)) != ORIGINAL_SHA256 or
            inputs != {name: sha256(read(sources / name)) for name in inputs} or
            sdk_inputs != {item.name: sha256(read(item)) for item in sorted(sdk_package.glob('*.py'))} or
            any(sha256(read(row['path'])) != row['sha256'] for row in tools.values())):
        raise ValueError('input changed during the build')
    result = dict(schema='aicwf-bip-patch-v2', state='assembled-not-executed',
                  original_sha256=ORIGINAL_SHA256, original_size=ORIGINAL_SIZE,
                  firmware_base=ORIGINAL_BASE, modified_sha256=modified_sha256,
                  kernel_header_sha256=sha256(header), inputs=inputs, tools=tools,
                  sdk_api=1, sdk_inputs=sdk_inputs,
                  python_version=sys.version.split()[0],
                  compiler_version=compiler_version, compiler_target='arm-none-eabi',
                  commands=commands, sections=rows, device_access=False,
                  execution_accepted=False, loader_accepted=False)
    jsonio.write_json(output / 'manifest.json', result)
    pending.unlink()
    if not any(temporary.iterdir()):
        temporary.rmdir()
    return output / 'manifest.json'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-source', required=True)
    parser.add_argument('--toolchain', required=True)
    parser.add_argument('--original', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    sys.dont_write_bytecode = True
    # Signal exceptions let the SDK drain the owned compiler session.
    def interrupted(unused_signum, unused_frame):
        raise KeyboardInterrupt
    for number in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(number, interrupted)
    try:
        print(build(args))
    except KeyboardInterrupt:
        print('interrupted; preserve incomplete output for inspection', file=sys.stderr)
        return 130
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
