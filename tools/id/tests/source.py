#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Inspect the native ID source contract. Do not compile or run C."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]


class IdSourceTests(unittest.TestCase):
    def test_process_group_count_comes_from_the_actual_query(self):
        source = (ROOT / 'usr.bin/id/id.c').read_text()
        self.assertNotIn('_SC_NGROUPS_MAX', source)
        self.assertIn('getgroups(0, NULL)', source)
        self.assertIn('errno != EINVAL', source)
        self.assertIn('GROUP_RETRIES', source)
        self.assertIn('user_groups(pw)', source)

    def test_host_abi_adapter_has_distinct_native_integer_storage(self):
        text = (ROOT / 'common/include/libutil/compat_grouplist.h').read_text()
        self.assertIn('int *native_groups', text)
        self.assertIn('getgrouplist(user, native_base, native_groups, count)', text)
        self.assertNotIn('(int *)', text)
        self.assertNotIn('mbr_', text)
        self.assertIn('return getgrouplist(user, base, groups, count)', text)

    def test_fixed_profile_keeps_source_and_existing_aliases(self):
        value = json.loads((ROOT / 'tools/id/bootstrap.json').read_bytes())
        self.assertEqual(value['sources'], ['usr.bin/id/id.c'])
        self.assertEqual(value['headers'], ['common/include/libutil/compat_grouplist.h',
                                          'tools/compat/bootstrap_cdefs.h'])
        self.assertEqual(value['invocations'], ['id', 'groups', 'whoami'])
        self.assertEqual(len(value['runtime_cases']), 31)
        self.assertEqual(value['files'], {})
        self.assertTrue(any(case.get('stdout_identity') == 'groups' for case in value['runtime_cases']))
        self.assertTrue(any(case.get('stdout_identity') == 'root-groups' for case in value['runtime_cases']))
        self.assertIn('HOST_SRCDIR=\tusr.bin/id', (ROOT / 'tools/id/Makefile').read_text())


if __name__ == '__main__':
    unittest.main()
