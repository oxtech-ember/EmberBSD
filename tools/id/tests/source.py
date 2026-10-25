#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Inspect the native ID source contract. Do not compile or run C."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[3]
DARWIN_REQUEST = (
    '#if defined(__APPLE__) && !defined(_DARWIN_UNLIMITED_GETGROUPS)\n'
    '#define _DARWIN_UNLIMITED_GETGROUPS 1\n'
    '#endif')


def require_full_darwin_groups(source):
    """Check source ordering without running a C preprocessor or native child."""
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    request = re.search(
        r'^\s*#if defined\(__APPLE__\) && !defined\(_DARWIN_UNLIMITED_GETGROUPS\)\n'
        r'#define _DARWIN_UNLIMITED_GETGROUPS 1\n#endif', source, flags=re.M)
    include = re.search(r'^\s*#\s*include\b', source, flags=re.M)
    if not request or not include or request.end() >= include.start():
        raise ValueError('Darwin group API request must precede every system include')
    if re.search(r'^\s*#\s*define\s+_DARWIN_C_SOURCE\b', source, flags=re.M):
        raise ValueError('The ID source must not select all Darwin extensions')


class IdSourceTests(unittest.TestCase):
    def test_darwin_group_API_is_requested_before_system_headers(self):
        require_full_darwin_groups((ROOT / 'usr.bin/id/id.c').read_text())

    def test_source_gate_refuses_missing_late_or_global_requests(self):
        includes = '#include <sys/cdefs.h>\n#include <unistd.h>\n'
        require_full_darwin_groups(DARWIN_REQUEST + '\n' + includes)
        for source in (
                includes,
                '#include <sys/cdefs.h>\n' + DARWIN_REQUEST + '\n#include <unistd.h>\n',
                includes + DARWIN_REQUEST,
                DARWIN_REQUEST.replace('__APPLE__', '__NetBSD__') + '\n' + includes,
                '#define _DARWIN_C_SOURCE 1\n' + DARWIN_REQUEST + '\n' + includes):
            with self.subTest(source=source), self.assertRaises(ValueError):
                require_full_darwin_groups(source)

    def test_bootstrap_forceinclude_does_not_declare_getgroups_early(self):
        text = (ROOT / 'tools/compat/bootstrap_cdefs.h').read_text()
        self.assertEqual(re.findall(r'^#include <([^>]+)>', text, flags=re.M),
                         ['sys/cdefs.h'])
        self.assertNotIn('getgroups', text)

    def test_normal_host_recipe_keeps_common_compatibility_integration(self):
        recipe = (ROOT / 'tools/id/Makefile').read_text()
        self.assertNotRegex(recipe, r'(?m)^NOCOMPATLIB\s*[:?+]?=')
        self.assertNotIn('bootstrap_cdefs.h', recipe)
        source = (ROOT / 'usr.bin/id/id.c').read_text()
        self.assertIn('#if HAVE_NBTOOL_CONFIG_H\n#include "nbtool_config.h"\n#endif', source)
        self.assertLess(source.index('#include "nbtool_config.h"'),
                        source.index('#include <sys/cdefs.h>'))

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
        self.assertIn('getgrouplist(user, native_base, native_groups,', text)
        self.assertIn('&native_count)', text)
        self.assertNotIn('(int *)', text)
        self.assertNotIn('mbr_', text)
        self.assertIn('return getgrouplist(user, base, groups, count)', text)

    def test_darwin_complete_count_sizing_has_an_int_width_bound(self):
        text = (ROOT / 'common/include/libutil/compat_grouplist.h').read_text()
        self.assertIn('#define COMPAT_GROUPLIST_RETRIES (sizeof(int) * CHAR_BIT)', text)
        self.assertIn('#define COMPAT_GROUPLIST_MAX (INT_MAX - 1)', text)
        self.assertIn('compat_grouplist_next', text)
        self.assertIn('native_count > capacity', text)
        source = (ROOT / 'usr.bin/id/id.c').read_text()
        self.assertIn('#define\tGROUP_RETRIES\t8', source)

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
