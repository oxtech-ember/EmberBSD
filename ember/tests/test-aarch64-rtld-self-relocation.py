#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Exercise the actual AArch64 loader's RELA self-relocation with host data.
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True, help='absolute selected native C compiler')
args = parser.parse_args()
cc = Path(args.cc)
if not cc.is_absolute() or not cc.is_file():
    parser.error('--cc must name the selected native compiler')
source = (Path(__file__).resolve().parents[2] / 'libexec/ld.elf_so/arch/aarch64/mdreloc.c').read_text()
start = source.index('void\n_rtld_relocate_nonplt_self(')
end = source.index('\nint\n_rtld_relocate_nonplt_objects(', start)
function = source[start:end]
harness = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uintptr_t Elf_Addr;
typedef struct { long d_tag; union { uintptr_t d_ptr; uintptr_t d_val; } d_un; } Elf_Dyn;
typedef struct { uintptr_t r_offset, r_info; intptr_t r_addend; } Elf_Rela;
#define DT_NULL 0
#define DT_RELA 7
#define DT_RELASZ 8
''' + function + r'''
int main(void) {
    union { uintptr_t align; unsigned char bytes[256]; } image;
    Elf_Rela *rela = (Elf_Rela *)(image.bytes + 32);
    Elf_Addr *slots = (Elf_Addr *)(image.bytes + 128);
    Elf_Dyn dyn[3] = {{DT_RELA, {.d_ptr = 32}},
        {DT_RELASZ, {.d_val = 2 * sizeof(*rela)}}, {DT_NULL, {.d_val = 0}}};
    memset(&image, 0, sizeof(image));
    rela[0].r_offset = 128; rela[0].r_addend = 0x1234;
    rela[1].r_offset = 128 + sizeof(*slots); rela[1].r_addend = 0x5678;
    slots[1] = 0x5678; /* Also works when a linker prepopulates the word. */
    _rtld_relocate_nonplt_self(dyn, (Elf_Addr)image.bytes);
    return slots[0] != (Elf_Addr)image.bytes + 0x1234 ||
        slots[1] != (Elf_Addr)image.bytes + 0x5678;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cfile = Path(tmp) / 'test.c'
    executable = Path(tmp) / 'test'
    cfile.write_text(harness)
    subprocess.run([str(cc), '-std=c11', '-Wall', '-Wextra', '-Werror', str(cfile), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('PASS: AArch64 loader RELA addends with empty and prefilled slots')
