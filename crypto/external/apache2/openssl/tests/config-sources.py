#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check source/object collisions and shared-only selection."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--perl', required=True)
parser.add_argument('--work-dir', required=True)
args = parser.parse_args()
script = Path(__file__).resolve().parents[1] / 'tools/config-sources.pl'
with tempfile.TemporaryDirectory(dir=args.work_dir) as directory:
    work = Path(directory)
    (work / 'a').mkdir()
    (work / 'b').mkdir()
    for name in ('a/foo.c', 'a/foo.S', 'b/foo.c', 'a/bar.c'):
        (work / name).write_text('')
    def check(static, shared, group, success, expected=''):
        (work / 'configdata.pm').write_text('''package configdata;
use Exporter 'import'; our @EXPORT = qw(%%unified_info); our %%unified_info = (
 sources => { libcrypto => [], 'providers/libdefault.a' => [], 'providers/libcommon.a' => [],
 libssl => [%s], obj1 => ['a/foo.c'], obj2 => ['a/foo.S'], obj3 => ['b/foo.c'], obj4 => ['a/bar.c'] },
 shared_sources => { libcrypto => [], libssl => [%s] }, defines => {}, includes => {});
1;
''' % (static, shared))
        result = subprocess.run([args.perl, '-I.', str(script), group], cwd=work,
                                capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stderr
        assert expected in (result.stdout if success else result.stderr), (result.stdout, result.stderr)
    check("'obj1','obj2'", '', 'ssl', False, 'duplicate object stem')
    check("'obj1'", "'obj3'", 'ssl-shared', False, 'conflicting shared object stem')
    check("'obj1'", "'obj1','obj4'", 'ssl-shared', True, 'SOBJS+= bar.pico')
    check('', '', 'crypto-shared', True)
print('PASS: object stem collisions and shared-only sources')
