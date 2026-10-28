#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check GenFw converts ordinary ELF images for two supported architectures."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('clang', 'ld', 'genfw', 'readobj'):
    parser.add_argument('--' + name, required=True, type=Path)
args = parser.parse_args()
for path in (args.clang, args.ld, args.genfw, args.readobj):
    if not path.is_absolute():
        parser.error('select absolute tool paths')
env = {'PATH': str(args.clang.parent)}
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as directory:
    root = Path(directory)
    source = root / 'main.c'
    source.write_text('int efi_main(void *image, void *system) { return image != system; }\n')
    for triplet, expected in (('aarch64-unknown-none-elf', 'COFF-ARM64'),
                              ('i386-unknown-none-elf', 'COFF-i386')):
        elf, efi = root / (triplet + '.elf'), root / (triplet + '.efi')
        subprocess.run([str(args.clang), '--target=' + triplet, '-ffreestanding',
                        '-fshort-wchar', '-nostdlib', '--ld-path=' + str(args.ld), '-Wl,-e,efi_main',
                        '-Wl,--emit-relocs', str(source), '-o', str(elf)], env=env, check=True)
        subprocess.run([str(args.genfw), '-e', 'UEFI_APPLICATION', '-o', str(efi), str(elf)],
                       env=env, check=True)
        result = subprocess.run([str(args.readobj), '--file-headers', str(efi)], env=env,
                                check=True, capture_output=True, text=True)
        if 'Format: ' + expected not in result.stdout:
            raise SystemExit('unexpected GenFw output: ' + result.stdout)
        print(triplet + ': PASS')
