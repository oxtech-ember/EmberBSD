#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Shared stat access source controls. Native typed controls are separate."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parent


class StatTimeSourceTests(unittest.TestCase):
    def test_common_header_preserves_all_three_real_member_families(self):
        path = ROOT / 'compat_stat_time.h'
        self.assertTrue(path.is_file(), 'the shared stat accessor is absent')
        text = path.read_text()
        self.assertIn('HAVE_STRUCT_STAT_ST_ATIM', text)
        self.assertIn('HAVE_STRUCT_STAT_ST_MTIMENSEC', text)
        self.assertNotIn('#define HAVE_', text)
        for field in ('a', 'm', 'c'):
            self.assertIn('__nbcompat_stat_' + field + 'time(const struct stat *st)', text)
            self.assertIn('st->st_' + field + 'tim);', text)
            self.assertIn('st->st_' + field + 'timensec', text)
            self.assertIn('st->st_' + field + 'timespec);', text)
        self.assertIn('compat_stat_time.h', (ROOT / 'Makefile').read_text())


if __name__ == '__main__':
    unittest.main()
