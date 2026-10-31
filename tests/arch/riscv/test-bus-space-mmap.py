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
pmap_header = (root / 'sys/arch/riscv/include/pmap.h').read_text()
pte_header = (root / 'sys/arch/riscv/include/pte.h').read_text()
uvm_param = (root / 'sys/uvm/uvm_param.h').read_text()

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
pmap_phys = re.search(r'^#define\s+pmap_phys_address\(x\).*$', pmap_header, re.M)
if not pmap_phys:
    raise ValueError('missing physical mmap cookie conversion')
pte_macros = []
for name in ('PTE_PPN_SHIFT', 'PA_TO_PTE', 'PTE_TO_PA'):
    found = re.search(r'^#define\s+' + name + r'(?:\([^\n]*?\))?\s+.*$', pte_header, re.M)
    if not found:
        raise ValueError('missing PTE macro ' + name)
    pte_macros.append(found.group())
page_macros = []
for name in ('atop', 'ptoa'):
    found = re.search(r'^#define\s+' + name + r'\(x\)\s+.*$', uvm_param, re.M)
    if not found:
        raise ValueError('missing page macro ' + name)
    page_macros.append(found.group())
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
#define PAGE_SHIFT 12
#define PGSHIFT 12
#define __BITS(h,l) ((UINT64_MAX >> (63-(h))) & (UINT64_MAX << (l)))
#define __SHIFTOUT(x,m) (((x) & (m)) >> __builtin_ctzll(m))
#if TEST_RV64
typedef uint64_t pt_entry_t;
#define PTE_PPN __BITS(53,10)
#else
typedef uint32_t pt_entry_t;
#define PTE_PPN __BITS(31,10)
#endif
struct bus_space { void *bs_cookie; int bs_stride; paddr_t (*bs_mmap)(void *,bus_addr_t,off_t,int,int); };
typedef struct bus_space *bus_space_tag_t;
paddr_t generic_bs_mmap(void *,bus_addr_t,off_t,int,int);
''' + '\n'.join(page_macros + pte_macros) + '\n' + tag('riscv_generic_bs_tag') + '\n' + tag('riscv_generic_a4x_bs_tag') + '\n' + function(source, 'generic_bs_mmap') + '\n' + function(mainbus, 'fdtbus_bus_tag_create') + '\n' + macro.group() + '\n' + pmap_phys.group() + r'''
struct pcih_bus_space { struct bus_space bst; int other; };
int main(void) {
 struct bus_space *normal = fdtbus_bus_tag_create(1, 0);
 struct bus_space *wide = &riscv_generic_a4x_bs_tag;
 assert(normal == &riscv_generic_bs_tag && normal->bs_cookie == normal);
 assert(wide->bs_cookie == wide);
 assert(bus_space_mmap(normal, 0x100000, 0x2000, 0, 0) == 0x102);
 assert(bus_space_mmap(wide, 0x100000, 0x2000, 0, 0) == 0x108);
 /* uvm_device passes the cookie through pmap_phys_address to pmap_enter. */
 assert(pmap_phys_address(bus_space_mmap(normal, 0x50ef5000, 0, 0, 0)) == 0x50ef5000);
 assert(PTE_TO_PA((pt_entry_t)PA_TO_PTE(pmap_phys_address(bus_space_mmap(normal, 0x50ef5000, 0, 0, 0)))) == 0x50ef5000);
 assert(pmap_phys_address(bus_space_mmap(wide, 0x100000, 0x2000, 0, 0)) == 0x108000);
 /* Generic wsdisplay drivers can return atop(pa) without bus_space_mmap. */
 assert(pmap_phys_address(atop(UINT64_C(0x120000000))) == UINT64_C(0x120000000));
 assert(PTE_TO_PA((pt_entry_t)PA_TO_PTE(pmap_phys_address(atop(UINT64_C(0x120000000))))) == UINT64_C(0x120000000));
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
    for rv64 in (0, 1):
        subprocess.run([a.cc, '-Werror', '-DTEST_RV64=' + str(rv64),
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(c), '-o', str(out)], check=True)
        subprocess.run([str(out)], check=True)
    target = Path(td) / 'target.c'
    target.write_text('typedef unsigned long long paddr_t;\n#define PAGE_SHIFT 12\n' +
                      '\n'.join(page_macros) + '\n' + pmap_phys.group() + '\n' +
                      '_Static_assert(pmap_phys_address(atop(0x50ef5000ULL)) == 0x50ef5000ULL, "framebuffer");\n' +
                      '_Static_assert(pmap_phys_address(atop(0x120000000ULL)) == 0x120000000ULL, "wide physical address");\n')
    for bits in (32, 64):
        subprocess.run([a.cc, '--target=riscv' + str(bits) + '--netbsd', '-nostdinc',
                        '-Werror', '-fsyntax-only', str(target)], check=True)
print('RV32/RV64: device mmap cookies convert to the intended physical PTE')
