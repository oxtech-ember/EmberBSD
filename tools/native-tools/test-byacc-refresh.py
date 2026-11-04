#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check the OS byacc producer declaration through the SDK refresh planner."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--sdk-source', required=True, type=Path)
args = parser.parse_args()
sys.path.insert(0, str(args.sdk_source.resolve() / 'src'))
from emberbsd_sdk import prepared_refresh

root = Path(__file__).resolve().parents[2]
entry = json.loads((root / prepared_refresh.CONTRACT).read_text())['sources']['EMBER_BYACC_SOURCE']
assert entry == {'producer_dirs': ['external/bsd/byacc/bin'],
                 'header_prefixes': [], 'consumer_dirs': [], 'linked_names': [],
                 'native_producer_dirs': ['tools/yacc']}
with tempfile.TemporaryDirectory(prefix='byacc-refresh-') as name:
    obj = Path(name)
    for directory in ('external/bsd/byacc/bin', 'tools/yacc', 'tools/unrelated'):
        target = obj / directory
        target.mkdir(parents=True)
        (target / 'preserved.o').write_bytes(b'object')
    assert prepared_refresh.plan_native(root, obj, {'byacc'}) == ['tools/yacc']
    assert prepared_refresh.plan(root, obj, {'byacc'}) == ['external/bsd/byacc/bin']
    assert all((obj / directory / 'preserved.o').read_bytes() == b'object'
               for directory in ('external/bsd/byacc/bin', 'tools/yacc', 'tools/unrelated'))
print('byacc scoped refresh planning PASS')
