#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the real RISC-V public mmap macro, generic helper and tags."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[3]
source = (root / 'sys/arch/riscv/riscv/bus_space.c').read_text()
mainbus = (root / 'sys/arch/riscv/riscv/mainbus.c').read_text()
funcs = (root / 'sys/arch/riscv/include/bus_funcs.h').read_text()
pci = (root / 'sys/arch/riscv/fdt/pcihost_fdt.c').read_text()
pci_header = (root / 'sys/arch/riscv/fdt/pcihost_fdtvar.h').read_text()

def function(text, name):
    m = re.search(r'\n(?:paddr_t|int|bus_space_tag_t)\n' + name + r'\(.*?\n\}', text, re.S)
    if not m:
        raise ValueError('missing function ' + name)
    return m.group()

def tag(name):
    m = re.search(r'struct bus_space ' + name + r' = \{(.*?)\n\};', source, re.S)
    if not m:
        raise ValueError('missing tag ' + name)
    body = m.group(1)
    fields = []
    for field in ('bs_cookie', 'bs_stride', 'bs_mmap'):
        x = re.search(r'^\s*\.' + field + r'\s*=\s*([^,]+),', body, re.M)
        fields.append('.' + field + ' = ' + (x.group(1).strip() if x else '0'))
    return 'struct bus_space ' + name + ' = { ' + ', '.join(fields) + ' };'

macro = re.search(r'^#define bus_space_mmap\(t, a, o, p, f\).*?\n\s*\(\*\(t\)->bs_mmap\).*?\n', funcs, re.M | re.S)
if not macro:
    raise ValueError('missing public bus_space_mmap macro')
if not re.search(r'struct pcih_bus_space\s*\{\s*struct bus_space\s+bst\s*;', pci_header):
    raise ValueError('PCI bus tag is no longer the first wrapper member')
copy = re.search(r'pibs->bst\s*=\s*\*sc->sc_pci_bst;\s*pibs->bst\.bs_cookie\s*=\s*pibs;', pci)
if not copy:
    raise ValueError('missing PCI copy and cookie initialization')
code = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef uint64_t bus_addr_t;
typedef int64_t off_t;
typedef uint64_t paddr_t;
typedef unsigned int uint32_t;
#define atop(x) ((x) >> 12)
struct bus_space { void *bs_cookie; int bs_stride; paddr_t (*bs_mmap)(void *,bus_addr_t,off_t,int,int); };
typedef struct bus_space *bus_space_tag_t;
paddr_t generic_bs_mmap(void *,bus_addr_t,off_t,int,int);
''' + tag('riscv_generic_bs_tag') + '\n' + tag('riscv_generic_a4x_bs_tag') + '\n' + function(source, 'generic_bs_mmap') + '\n' + function(mainbus, 'fdtbus_bus_tag_create') + '\n' + macro.group() + r'''
struct pcih_bus_space { struct bus_space bst; int other; };
int main(void) {
 struct bus_space *normal = fdtbus_bus_tag_create(1, 0);
 struct bus_space *wide = &riscv_generic_a4x_bs_tag;
 assert(normal == &riscv_generic_bs_tag && normal->bs_cookie == normal);
 assert(wide->bs_cookie == wide);
 assert(bus_space_mmap(normal, 0x100000, 0x2000, 0, 0) == 0x102);
 assert(bus_space_mmap(wide, 0x100000, 0x2000, 0, 0) == 0x108);
 struct pcih_bus_space copied, *pibs = &copied;
 struct pcihost_softc { struct bus_space *sc_pci_bst; } holder = { wide }, *sc = &holder;
 assert(offsetof(struct pcih_bus_space, bst) == 0);
''' + copy.group() + r'''
 assert(bus_space_mmap(&copied.bst, 0x100000, 0x2000, 0, 0) == 0x108);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'test.c'
    out = Path(td) / 'test'
    c.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(c), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('FDT and copied RISC-V bus tags map stride 0/2 and preserve the cookie')
