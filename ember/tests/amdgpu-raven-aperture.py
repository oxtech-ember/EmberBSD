#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Exercise the actual APU aperture selection for Raven and other devices."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'sys/external/bsd/drm2/dist/drm/amd/amdgpu/amdgpu_gmc_v9_0.c').read_text()
start = source.index('\tif ((adev->flags & AMD_IS_APU)', source.index('static int gmc_v9_0_mc_init'))
end = source.index('\n#endif', source.index('\n\t}', start))
selection = source[start:end]
test = r'''
#include <assert.h>
#include <stdint.h>
#ifdef __NetBSD__
#undef __NetBSD__
#endif
#if TEST_NETBSD
#define __NetBSD__ 1
#endif
#define AMD_IS_APU 1
#define CHIP_RAVEN 2
#define PAGE_SIZE 4096
#define PCI_MAPREG_TYPE(x) ((x) & 1)
#define PCI_MAPREG_TYPE_MEM 0
struct pci_dev { struct { int type; } pd_resources[1]; };
struct gpu {
 int flags, asic_type;
 struct pci_dev *pdev;
 struct { uint64_t aper_base, aper_size, real_vram_size; } gmc;
};
#define gfxhub_v1_0_get_mc_fb_offset(x) 0x300000000ULL
static void select_aperture(struct gpu *adev) {
''' + selection + r'''
}
int main(void) {
 struct pci_dev pci = {0};
 struct gpu a = { AMD_IS_APU, CHIP_RAVEN, &pci,
                  { 0xe0000000, 0x10000000, 0x20000000 } };
 select_aperture(&a);
#if TEST_NETBSD
 assert(a.gmc.aper_base == 0xe0000000 && a.gmc.aper_size == 0x10000000);
 a.gmc.aper_size = 0;
 select_aperture(&a);
 assert(a.gmc.aper_base == 0x300000000ULL);
 a.gmc.aper_base = 0xe0000000; a.gmc.aper_size = PAGE_SIZE - 1;
 select_aperture(&a);
 assert(a.gmc.aper_base == 0x300000000ULL && a.gmc.aper_size == 0x20000000);
 a.gmc.aper_base = 0; a.gmc.aper_size = PAGE_SIZE;
 select_aperture(&a);
 assert(a.gmc.aper_base == 0x300000000ULL);
 a.gmc.aper_base = 0xe0000000; a.gmc.aper_size = PAGE_SIZE;
 pci.pd_resources[0].type = 1;
 select_aperture(&a);
 assert(a.gmc.aper_base == 0x300000000ULL);
 pci.pd_resources[0].type = 0; a.gmc.aper_base = 0xe0000000;
 a.gmc.aper_size = PAGE_SIZE;
 select_aperture(&a);
 assert(a.gmc.aper_base == 0xe0000000 && a.gmc.aper_size == PAGE_SIZE);
 a.asic_type = 3;
 select_aperture(&a);
 assert(a.gmc.aper_base == 0x300000000ULL);
#else
 assert(a.gmc.aper_base == 0x300000000ULL && a.gmc.aper_size == 0x20000000);
#endif
 a.flags = 0; a.gmc.aper_base = 0xe0000000; a.gmc.aper_size = PAGE_SIZE;
 select_aperture(&a);
 assert(a.gmc.aper_base == 0xe0000000 && a.gmc.aper_size == PAGE_SIZE);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='raven-aperture-test-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(test)
    for netbsd in (0, 1):
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-DTEST_NETBSD=' + str(netbsd), str(path / 'test.c'),
                        '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
print('PASS: actual APU aperture selection')
