#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Source-owned host and finite ls contracts. No native child is executed."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
UNITS = ('cmp', 'ls', 'main', 'print', 'util')


class SourceContract(unittest.TestCase):
    def test_complete_host_route_and_no_reduced_build(self):
        recipe = (ROOT / 'tools/ls/Makefile').read_text()
        self.assertIn('HOST_SRCDIR=\tbin/ls', recipe)
        self.assertIn('Makefile.host', recipe)
        self.assertNotIn('NOCOMPATLIB', recipe)
        self.assertNotIn('-DSMALL', recipe)
        for unit in UNITS:
            text = (ROOT / f'bin/ls/{unit}.c').read_text()
            self.assertLess(text.index('#include "nbtool_config.h"'), text.index('#include <sys/cdefs.h>'))

    def test_host_acl_calls_real_shared_adapter(self):
        text = (ROOT / 'bin/ls/print.c').read_text()
        self.assertIn('__nbcompat_acl_present_nofollow(name, &present)', text)
        self.assertIn('acl_get_link_np(name, type)', text)
        self.assertIn('warn("%s", name)', text)

    def test_typed_nanosecond_access_and_integer_width(self):
        self.assertFalse((ROOT / 'bin/ls/ls_stat.h').exists())
        text = (ROOT / 'bin/ls/cmp.c').read_text()
        host = text.split('#if HAVE_NBTOOL_CONFIG_H\n#include "compat_stat_time.h"', 1)[1]
        host, target = host.split('#else', 1)
        for stem in ('a', 'm', 'c'):
            self.assertIn('__nbcompat_stat_' + stem + 'time(x).tv_nsec', host)
            self.assertIn('(x)->st_' + stem + 'timensec', target)
            self.assertIn('(x)->st_' + stem + 'timespec.tv_nsec', target)
        self.assertNotIn('HAVE_STRUCT_STAT_ST_', host)
        self.assertIn('SIZE_MAX/len', (ROOT / 'bin/ls/util.c').read_text())

    def test_required_api_control_and_exact_source_roster(self):
        manifest = json.loads((ROOT / 'tools/ls/bootstrap.json').read_text())
        self.assertEqual(manifest['sources'], [f'bin/ls/{unit}.c' for unit in UNITS])
        self.assertEqual(manifest['components'], ['listing'])
        self.assertEqual(manifest['control'], 'tools/ls/probes/control.c')
        self.assertEqual(manifest['headers'], ['bin/ls/ls.h', 'bin/ls/extern.h',
                                               'tools/compat/compat_stat_time.h'])
        self.assertEqual(manifest['include_dirs'], ['bin/ls', 'tools/compat'])
        self.assertEqual(len(manifest['runtime_cases']), 38)
        control = (ROOT / manifest['control']).read_text()
        for name in ('fts_open', 'fts_children', 'fts_read', 'fts_set', 'fts_close',
                     'openat', 'fstatat', 'futimens', 'strvis', 'strmode',
                     'user_from_uid', 'group_from_gid', 'mbrtowc', 'wcrtomb', 'wcwidth'):
            self.assertIn(name + '(', control)
        self.assertNotIn('system(', control)
        self.assertIn('#include \"compat_stat_time.h\"', control)
        self.assertNotIn('ls_stat.h', control)
        for stem in ('a', 'm', 'c'):
            self.assertIn('__nbcompat_stat_' + stem + 'time(&st)', control)
        for stem, index in (('a', 0), ('m', 1)):
            for field in ('tv_sec', 'tv_nsec'):
                self.assertIn(f'__nbcompat_stat_{stem}time(&st).{field} != times[{index}].{field}', control)
        self.assertIn('123456789', control)
        self.assertIn('987654321', control)

    def test_time_sorting_oracles_are_independent_of_names(self):
        manifest = json.loads((ROOT / 'tools/ls/bootstrap.json').read_text())
        cases = {case['name']: case for case in manifest['runtime_cases']}
        for name in ('mtime-nanoseconds', 'mtime-reverse', 'mtime-tie', 'atime-nanoseconds'):
            case = cases[name]
            fixtures = case['filesystem']['fixtures']
            field = 'atime_ns' if '-u' in case['args'] else 'mtime_ns'
            values = {path: row[field] for path, row in fixtures.items()}
            expected = sorted(values, key=lambda path: (-values[path], path))
            if '-r' in case['args']:
                expected.reverse()
            self.assertEqual(case['stdout'], ''.join(path + '\n' for path in expected))
        nano = cases['mtime-nanoseconds']['filesystem']['fixtures']
        self.assertEqual(nano['a']['mtime_ns'] // 10**9, nano['z']['mtime_ns'] // 10**9)
        self.assertNotEqual(nano['a']['mtime_ns'], nano['z']['mtime_ns'])
        self.assertEqual(cases['mtime-nanoseconds']['stdout'], 'z\na\n')

    def test_across_columns_retains_canonical_trailing_padding(self):
        manifest = json.loads((ROOT / 'tools/ls/bootstrap.json').read_text())
        case = next(case for case in manifest['runtime_cases'] if case['name'] == 'across-single')
        self.assertEqual(case['args'], ['-x', 'name'])
        self.assertEqual(case['stdout'], 'name \n')
        source = (ROOT / 'bin/ls/print.c').read_text()
        body = source.split('printacol(DISPLAY *dp)', 1)[1].split('printstream(DISPLAY *dp)', 1)[0]
        self.assertIn('while (chcnt++ < colwidth)', body)


if __name__ == '__main__':
    unittest.main()
