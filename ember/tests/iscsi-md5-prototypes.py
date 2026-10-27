#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check iSCSI MD5 definitions retain their typed, promoted signatures."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
base = root / 'external/bsd/iscsi'
c = (base / 'dist/src/lib/md5c.c').read_text()
h = (base / 'dist/src/lib/md5hl.c').read_text()
make = (base / 'lib/Makefile').read_text()
for signature in (
    'Encode(unsigned char *output, UINT4 *input, unsigned int len)',
    'Decode(UINT4 *output, const unsigned char *input, unsigned int len)',
    'iSCSI_MD5Transform(UINT4 state[4], const unsigned char block[64])',
):
    assert signature in c, signature
assert 'MDNAME(File)(const char *filename, char *buf)' in h
assert 'Wno-error=deprecated-non-prototype' not in make
print('iSCSI MD5 prototype contract: passed')
