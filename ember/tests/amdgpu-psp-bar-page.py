#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Compile and exercise the actual Raven PSP BAR page helper."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'sys/external/bsd/drm2/dist/drm/amd/amdgpu/amdgpu_psp_v10_0.c').read_text()
start = source.index('static bool\npsp_v10_bar_page(')
end = source.index('\n#endif', start)
helper = source[start:end]
test = r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#define PAGE_SIZE 4096
#if MASK_POLARITY
#define PAGE_MASK (~(PAGE_SIZE-1))
#else
#define PAGE_MASK (PAGE_SIZE-1)
#endif
''' + helper + r'''
int main(void) {
 uint64_t p, w;
 assert(psp_v10_bar_page(0xe0000000, 0x10000000, 0xbbe000, UINT32_MAX, &p, &w));
 assert(p == 0xe0bbe000 && w == 0);
 assert(psp_v10_bar_page(0xe0000000, 0x10000000, 0xbbe004, UINT32_MAX, &p, &w));
 assert(p == 0xe0bbe000 && w == 4);
 assert(psp_v10_bar_page(0xe0000000, 0x10000000, 0xfffffff, UINT32_MAX, &p, &w));
 assert(p == 0xeffff000 && w == 4095);
 assert(!psp_v10_bar_page(0, 0x1000, 0, UINT32_MAX, &p, &w));
 assert(!psp_v10_bar_page(0xe0000000, 0xfff, 0, UINT32_MAX, &p, &w));
 assert(!psp_v10_bar_page(0xe0000001, 0x10000000, 0, UINT32_MAX, &p, &w));
 assert(!psp_v10_bar_page(0xe0000000, 0x10000000, 0x10000000, UINT32_MAX, &p, &w));
 assert(!psp_v10_bar_page(0xfffff000, 0x1000, 0, UINT32_MAX, &p, &w));
 assert(psp_v10_bar_page(0x100000000, 0x1000, 0, UINT64_MAX, &p, &w));
 assert(!psp_v10_bar_page(UINT64_MAX - 4095, 0x1000, 0, UINT64_MAX, &p, &w));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='psp-bar-test-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(test)
    for polarity in (0, 1):
        subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-DMASK_POLARITY=' + str(polarity), str(path / 'test.c'),
                        '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
print('PASS: actual PSP BAR helper boundaries')
