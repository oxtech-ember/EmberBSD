#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check the narrow rm source contract. No compiler or native child runs."""
import hashlib
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
CONFIG_INCLUDE = '#if HAVE_NBTOOL_CONFIG_H\n#include "nbtool_config.h"\n#endif\n\n'
OLD_OPEN = '\tif ((fd = open(file, O_RDWR|O_SYNC|O_RSYNC|O_NOFOLLOW, 0)) == -1)\n'
NEW_OPEN = ('\tflags = O_RDWR | O_SYNC | O_NOFOLLOW;\n'
            '#if !defined(__APPLE__) || defined(O_RSYNC)\n'
            '\tflags |= O_RSYNC;\n#endif\n'
            '\tif ((fd = open(file, flags, 0)) == -1)\n')
CACHE_CONTROL = ('#if defined(__APPLE__) && !defined(O_RSYNC)\n'
    '\t/*\n'
    '\t * Request cache control before overwrite I/O. Small or misaligned\n'
    '\t * I/O can still use cached paths. Keep the explicit flush/readback.\n'
    '\t */\n'
    '\tif (fcntl(fd, F_NOCACHE, 1) == -1)\n'
    '\t\tgoto err;\n#endif\n\n')
ORIGINAL_SOURCE_SHA256 = '037e226348051f1ec4b4fe544d33436461b919beafae047a37450fea144a0318'


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / 'bin/rm/rm.c').read_text()

    def test_native_flags_keep_sync_and_preserve_other_hosts(self):
        self.assertIn(NEW_OPEN, self.source)
        self.assertNotRegex(self.source, r'#\s*define\s+(?:O_RSYNC|F_NOCACHE)\b')

    def test_cache_control_is_checked_after_identity_before_any_pass(self):
        s = self.source
        self.assertEqual(s.count(CACHE_CONTROL), 1)
        where = s.index(CACHE_CONTROL)
        self.assertGreater(where, s.index('fstat(fd, &sb2)'))
        self.assertGreater(where, s.index('sb2.st_dev != sbp->st_dev'))
        self.assertLess(where, s.index('#define RAND_BYTES'))

    def test_only_the_reviewed_portability_blocks_change_canonical_logic(self):
        s = self.source
        for block in (CONFIG_INCLUDE, NEW_OPEN, CACHE_CONTROL):
            self.assertEqual(s.count(block), 1)
        original = s.replace(CONFIG_INCLUDE, '').replace(NEW_OPEN, OLD_OPEN)
        original = original.replace(CACHE_CONTROL, '').replace('int fd, flags, randint;', 'int fd, randint;')
        self.assertEqual(hashlib.sha256(original.encode()).hexdigest(), ORIGINAL_SOURCE_SHA256)

    def test_four_passes_readback_and_error_before_unlink_remain(self):
        s = self.source
        self.assertEqual(len(re.findall(r'^\tWRITE_PASS\(', s, re.M)), 4)
        self.assertEqual(len(re.findall(r'^\tREAD_PASS\(', s, re.M)), 1)
        self.assertLess(s.index('WRITE_PASS(THIS_BYTE, 0x00)'), s.index('READ_PASS(0x00)'))
        self.assertLess(s.index('READ_PASS(0x00)'), s.index('WRITE_PASS(RAND_BYTES'))
        self.assertEqual(s.count('if (fsync(fd) || lseek(fd, (off_t)0, SEEK_SET))'), 2)
        for caller in ('rm_overwrite(p->fts_accpath, NULL)', 'rm_overwrite(f, &sb)'):
            self.assertRegex(s, re.escape(caller) + r'\)\s*continue;')
        self.assertIn('err:\teval = 1;', s)

    def test_normal_host_recipe_keeps_common_compatibility(self):
        s = (ROOT / 'tools/rm/Makefile').read_text()
        self.assertNotIn('NOCOMPATLIB', s)
        self.assertIn('HOST_SRCDIR=\tbin/rm', s)
        self.assertIn('.include "${.CURDIR}/../Makefile.host"', s)
        self.assertIn(CONFIG_INCLUDE + '#include <sys/cdefs.h>', self.source)
        self.assertEqual(hashlib.sha256((ROOT / 'bin/rm/Makefile').read_bytes()).hexdigest(),
                         '3a1836c32d8158a7d7cedc05c3f268b9867bdcd733ca1aeb93a8b7e1093958ac')

    def test_overwrite_cases_have_unique_files_and_bounded_literal_inputs(self):
        manifest = json.loads((ROOT / 'tools/rm/bootstrap.json').read_text())
        self.assertEqual(len(manifest['runtime_cases']), 25)
        wanted = {'overwrite-empty': '', 'overwrite-one': 'x',
                  'overwrite-block': [{'text': 'x', 'repeat': 8192}],
                  'overwrite-tail': [{'text': 'x', 'repeat': 8193}]}
        selected = {row['name']: row for row in manifest['runtime_cases'] if '-P' in row['args']}
        self.assertEqual(set(selected), set(wanted))
        for name, data in wanted.items():
            row = selected[name]
            self.assertEqual(row['args'], ['-P', 'victim'])
            self.assertEqual(row['filesystem']['fixtures'],
                             {'victim': {'type': 'file', 'mode': '0600', 'data': data}})
            self.assertEqual(row['filesystem']['expect'], {'victim': {'type': 'absent'}})
        self.assertTrue({'hardlink', 'whiteout-existing', 'whiteout-missing'} <=
                        {row['name'] for row in manifest['runtime_cases']})


if __name__ == '__main__':
    unittest.main()
