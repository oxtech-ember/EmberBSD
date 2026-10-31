#!/usr/bin/env python3
"""Exercise the actual ioctl cipher-set loop at the signed-shift boundary."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
s = (Path(__file__).resolve().parents[1] / 'ieee80211_ioctl.c').read_text()
m = re.search(r'\t\tcaps = 0;\n\t\tfor \(j = 1; j < 32; j\+\+\).*?caps \|= 1U << j;', s, re.S)
assert m
code = r'''
#include <assert.h>
#include <stdint.h>
struct request { int16_t i_val; };
struct common { unsigned ic_caps; };
static int cipher2cap(int j) { (void)j; return 0; }
static int ieee80211_crypto_available(int j) { return j == 31; }
int main(void) {
 struct request req = { -1 }, *ireq = &req;
 struct common common = { 0 }, *ic = &common;
 unsigned int caps; int j;
''' + m.group() + r'''
 assert(caps == 0x80000000U);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    source = Path(td) / 'cipher.c'
    binary = Path(td) / 'cipher'
    source.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('actual ioctl cipher mask loop handles bit 31 without signed shift')
