#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Exercise real native BFD helpers on a selected ELF fixture."""
import argparse
import os
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for name in ('binutils-prefix', 'dbsym', 'mdsetimage', 'work'):
    parser.add_argument('--' + name, required=True, type=Path)
parser.add_argument('--target', choices=('aarch64', 'amd64'), default='aarch64')
parser.add_argument('--elfedit', type=Path, help='installed OS elfedit for the compatibility-test path')
args = parser.parse_args()
work = args.work.resolve()
work.mkdir(exist_ok=False)
env = os.environ.copy()
env.pop('LD_LIBRARY_PATH', None)


def run(command, *arguments, expected=0):
    result = subprocess.run([str(command), *map(str, arguments)], cwd=work,
                            env=env, text=True, capture_output=True)
    with (work / 'commands.log').open('a') as stream:
        stream.write(repr(result.args) + '\n' + result.stdout + result.stderr)
    if result.returncode != expected:
        raise SystemExit('BFD helper failed; inspect ' + str(work / 'commands.log'))
    return result.stdout


assembly = '.text\n.globl _start\n_start: nop\n.data\n.balign 8\n'
for name, marker, capacity in (('db_symtab', 'EMBER-SYMBOLS-001', 8192),
                               ('md_root_image', 'EMBER-RAMDISK-01', 1024)):
    size = 'db_symtabsize' if name == 'db_symtab' else 'md_root_size'
    assembly += (f'.ascii "{marker}"\n.globl _{size}\n_{size}: .long {capacity}\n'
                 f'.globl _{name}\n_{name}: .fill {capacity},1,0\n')
(work / 'input.s').write_text(assembly)
binutils = args.binutils_prefix.resolve() / 'bin'
triplet = 'aarch64--netbsd' if args.target == 'aarch64' else 'x86_64--netbsd'
run(binutils / (triplet + '-as'), '-o', 'input.o', 'input.s')
run(binutils / (triplet + '-ld'), '-Ttext=0x10000', '-o', 'kernel.elf', 'input.o')
kernel = work / 'kernel.elf'
before = kernel.read_bytes()
marker = b'EMBER-SYMBOLS-001'
if before.count(marker) != 1:
    raise SystemExit('ambiguous symbol buffer marker')
size_offset = before.index(marker) + len(marker)
size, capacity = map(int, run(args.dbsym, '-p', kernel).split())
if not 0 < size <= capacity == 8192:
    raise SystemExit('unexpected symbol table size/capacity')
run(args.dbsym, kernel)
after = kernel.read_bytes()
if struct.unpack_from('<I', after, size_offset)[0] != size:
    raise SystemExit('dbsym did not store the real symbol table size')
if after[size_offset + 4:size_offset + 8] != b'\x7fELF':
    raise SystemExit('dbsym did not embed an ELF symbol table')
if before[:size_offset] != after[:size_offset] or before[size_offset + 4 + capacity:] != after[size_offset + 4 + capacity:]:
    raise SystemExit('dbsym wrote outside the reserved symbol region')
payload = bytes(range(256)) + b'EmberBSD BFD helper roundtrip\n'
(work / 'image.bin').write_bytes(payload)
run(args.mdsetimage, '-s', kernel, 'image.bin')
run(args.mdsetimage, '-x', kernel, 'roundtrip.bin')
if (work / 'roundtrip.bin').read_bytes() != payload:
    raise SystemExit('mdsetimage insert/extract changed the payload')
(work / 'too-large.bin').write_bytes(b'x' * 1025)
before = kernel.read_bytes()
run(args.mdsetimage, kernel, 'too-large.bin', expected=1)
if kernel.read_bytes() != before:
    raise SystemExit('rejected oversized image changed the kernel')
if args.elfedit is not None:
    image = work / 'linux-abi.elf'
    before = kernel.read_bytes()
    image.write_bytes(before)
    run(args.elfedit, '--output-osabi', 'Linux', image)
    after = image.read_bytes()
    if after[7] != 3 or before[:7] != after[:7] or before[8:] != after[8:]:
        raise SystemExit('elfedit did not change only the requested ELF OSABI')
    print('installed elfedit changes only the ELF OSABI for the Linux compatibility-test path')
print('real ' + args.target + ' dbsym embedding and mdsetimage insert/extract/refusal passed')
