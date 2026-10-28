#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Check AArch64 EFI conversion layout and the self-relocator's inputs."""
import argparse
from pathlib import Path
import struct


def check(elf, pe):
    def get(fmt, data, offset):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError('truncated image')
        return struct.unpack_from(fmt, data, offset)

    def need(condition, message):
        if not condition:
            raise ValueError(message)

    need(elf[:6] == b'\x7fELF\x02\x01' and get('<H', elf, 18)[0] == 183, 'expected AArch64 ELF64')
    shoff = get('<Q', elf, 40)[0]
    entsize, count, namesidx = get('<HHH', elf, 58)
    need(entsize >= 64 and 0 < namesidx < count, 'invalid ELF sections')
    sections = [get('<IIQQQQIIQQ', elf, shoff + i * entsize) for i in range(count)]
    names = sections[namesidx]
    need(names[4] + names[5] <= len(elf), 'invalid ELF names')
    strings = elf[names[4]:names[4] + names[5]]
    def name(row):
        end = strings.find(b'\0', row[0])
        need(row[0] < len(strings) and end >= 0, 'invalid section name')
        return strings[row[0]:end].decode('ascii')
    sections = {name(row): row for row in sections}
    need('.dynamic' in sections and '.rela.dyn' in sections, 'missing runtime sections')
    need(pe[:2] == b'MZ', 'missing DOS header')
    off = get('<I', pe, 0x3c)[0]
    need(pe[off:off + 4] == b'PE\0\0', 'missing PE signature')
    machine, count, _, _, _, optsize, _ = get('<HHIIIHH', pe, off + 4)
    opt = off + 24
    need(machine == 0xaa64 and get('<H', pe, opt)[0] == 0x20b, 'expected PE32+ ARM64')
    entry = get('<I', pe, opt + 16)[0]
    image_size, headers = get('<II', pe, opt + 56)
    alignment = get('<I', pe, opt + 32)[0]
    need(alignment > 0 and image_size % alignment == 0, 'PE SizeOfImage is not section-aligned')
    need(get('<H', pe, opt + 68)[0] == 10, 'not an EFI application')
    need(headers <= len(pe) and headers <= image_size, 'invalid PE headers')
    maps = []
    for i in range(count):
        sec = opt + optsize + 40 * i
        size, va, rawsize, raw = get('<IIII', pe, sec + 8)
        flags = get('<I', pe, sec + 36)[0]
        need(raw + rawsize <= len(pe) and va + size <= image_size, 'PE section outside image')
        maps.append((va, va + size, raw, rawsize, flags))
    maps.sort()
    need(all(maps[i - 1][1] <= maps[i][0] for i in range(1, len(maps))), 'overlapping PE sections')
    def mapped(addr, size, flag=0):
        for start, end, raw, rawsize, flags in maps:
            if start <= addr and addr + size <= end and addr - start + size <= rawsize and flags & flag == flag:
                return pe[raw + addr - start:raw + addr - start + size]
        raise ValueError('runtime range outside appropriate PE section')
    mapped(entry, 1, 0x20000000)
    for n in ('.dynamic', '.rela.dyn'):
        row = sections[n]; addr, offset, size = row[3:6]
        need(offset + size <= len(elf), 'ELF section outside file')
        need(mapped(addr, size) == elf[offset:offset + size], n + ' differs in PE')
    # These tables sit after _edata; objcopy retains them as file payload.
    # The AArch64 self-relocator uses DT_RELA and does not dereference them.
    for n in ('.dynsym', '.dynstr'):
        need(n in sections, 'missing ' + n)
        row = sections[n]; addr, offset, size = row[3:6]
        need(offset + size <= len(elf) and addr + size <= len(pe), n + ' outside file')
        need(pe[addr:addr + size] == elf[offset:offset + size], n + ' differs in PE file')
    dyn = sections['.dynamic']; tags = {}
    terminated = False
    for i in range(dyn[4], dyn[4] + dyn[5], 16):
        tag, value = get('<qQ', elf, i)
        if tag == 0:
            terminated = True
            break
        tags[tag] = value
    need(terminated, 'DT_NULL missing from .dynamic')
    rela = sections['.rela.dyn']
    need(tags.get(7) == rela[3] and tags.get(8) == rela[5] and tags.get(9) == 24, 'DT_RELA mismatch')
    need(rela[5] > 0 and rela[5] % 24 == 0, 'invalid relocation table')
    relative = 0
    for i in range(rela[4], rela[4] + rela[5], 24):
        target, info, addend = get('<QQq', elf, i)
        kind = info & 0xffffffff
        need(kind in (0, 1027) and info >> 32 == 0, 'unsupported relocation')
        if kind == 1027:
            relative += 1
            mapped(target, 8, 0x80000000)
            need(0 <= addend < image_size, 'relocation addend outside image')
    need(relative > 0, 'no AArch64 relative relocations')
    return relative


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--pe', type=Path, required=True)
    args = parser.parse_args()
    print('AArch64 EFI layout: PASS, relative relocations:', check(args.elf.read_bytes(), args.pe.read_bytes()))


if __name__ == '__main__':
    main()
