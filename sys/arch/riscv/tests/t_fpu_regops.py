#!/usr/bin/env python3
"""Compile the real F-only register operations and model their offsets."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
include = Path(__file__).resolve().parents[1] / 'riscv'
source = r'''
#include "rv32-fpu.h"
void save(void *p) { __asm__ volatile(RISCV_FPU32_STORE :: "r"(p), "i"(8) : "memory"); }
void load(const void *p) { __asm__ volatile(RISCV_FPU32_LOAD :: "r"(p), "i"(8) : "memory"); }
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td)/'probe.c'; asm = Path(td)/'probe.s'
    c.write_text(source)
    subprocess.run([a.cc, '--target=riscv32--netbsd', '-march=rv32imac_zicsr',
                    '-mabi=ilp32', '-ffreestanding', '-I'+str(include),
                    '-S', str(c), '-o', str(asm)], check=True)
    text = asm.read_text()
    names = ['ft0','ft1','ft2','ft3','ft4','ft5','ft6','ft7','fs0','fs1',
             'fa0','fa1','fa2','fa3','fa4','fa5','fa6','fa7','fs2','fs3',
             'fs4','fs5','fs6','fs7','fs8','fs9','fs10','fs11','ft8','ft9','ft10','ft11']
    expected = [(name, 8*i) for i, name in enumerate(names)]
    for op in ('fsw', 'flw'):
        found = [(m.group(1), int(m.group(2))) for m in re.finditer(
            r'\b'+op+r'\s+(\w+),\s*(-?\d+)\(\w+\)', text)]
        assert found == expected, (op, found)
    assert not re.search(r'\b(?:fsd|fld)\b', text)
    slots = [0]*32; before = [0x10000+i for i in range(32)]
    for i, (_, offset) in enumerate(expected): slots[offset//8] = before[i]
    after = [slots[offset//8] for _, offset in expected]
    assert after == before
    c.write_text(source.replace('RISCV_FPU32_', 'RISCV_FPU64_'))
    subprocess.run([a.cc, '--target=riscv64--netbsd', '-march=rv64imafdc_zicsr',
                    '-mabi=lp64d', '-ffreestanding', '-I'+str(include),
                    '-S', str(c), '-o', str(asm)], check=True)
    double = asm.read_text()
    for op in ('fsd', 'fld'):
        found = [(m.group(1), int(m.group(2))) for m in re.finditer(
            r'\b'+op+r'\s+(\w+),\s*(-?\d+)\(\w+\)', double)]
        assert found == expected, (op, found)
    assert not re.search(r'\b(?:fsw|flw)\b', double)
print('F-only and D register mapping and target compilation passed')
