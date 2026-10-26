#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Source checks; no native touch is executed.
from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def body(source, name):
    start = source.index('\n' + name + '(')
    start = source.index('{', start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def check_errno(source):
    for name, call in (('stime_arg0', 'parsedate(arg, NULL, NULL)'),
                       ('stime_arg1', 'mktime(t)'), ('stime_arg2', 'mktime(t)')):
        text = body(source, name)
        if not re.search(r'errno = 0;\s+tsp\[[01]\]\.tv_sec = tsp\[[01]\]\.tv_sec = ' +
                         re.escape(call) + ';', text):
            raise ValueError(name + ' must clear stale errno immediately before conversion')
        if 'tsp[0].tv_sec == NO_TIME && errno != 0' not in text:
            raise ValueError(name + ' must distinguish valid epoch -1 from conversion failure')
        if name != 'stime_arg0' and '|| difftm(t, &tm)' not in text:
            raise ValueError(name + ' must retain normalized-calendar validation')


class TouchSourceTests(unittest.TestCase):
    def test_fixed_timestamp_oracles_and_component_roster(self):
        manifest = json.loads((ROOT/'tools/touch/bootstrap.json').read_text())
        cases = {row['name']: row for row in manifest['runtime_cases']}
        self.assertEqual(len(cases), 36)
        self.assertEqual(manifest['components'], ['filetime', 'parsedate'])
        self.assertEqual(manifest['sources'], ['usr.bin/touch/touch.c'])
        self.assertEqual(manifest['headers'], [])
        for name in ('parser-minus-one', 'iso-minus-one', 'numeric-minus-one'):
            value = cases[name]['filesystem']['expect']['file']
            self.assertEqual((value['atime_ns'], value['mtime_ns']), (-1000000000, -1000000000))
        self.assertEqual(cases['parser-month-name']['filesystem']['expect']['file']['mtime_ns'],
                         946684800000000000)
        self.assertEqual(cases['parser-zone']['filesystem']['expect']['file']['mtime_ns'],
                         946665000000000000)
        self.assertEqual(cases['invalid-date']['exit'], 1)
        self.assertEqual(cases['invalid-date']['stderr'], "touch: Could not parse `not-a-date'\n")
        self.assertEqual(cases['dangling-link']['filesystem']['expect']['missing'], {'type': 'absent'})
        self.assertEqual(cases['hardlink']['filesystem']['expect']['alias']['same_inode_as'], 'file')

    def test_minus_one_uses_actual_conversion_errno(self):
        check_errno((ROOT/'usr.bin/touch/touch.c').read_text())

    def test_causal_errno_and_calendar_mutants_refuse(self):
        source = (ROOT/'usr.bin/touch/touch.c').read_text()
        check_errno(source)
        for name in ('stime_arg0', 'stime_arg1', 'stime_arg2'):
            original = body(source, name)
            for changed in (original.replace('errno = 0;', '', 1),
                            original.replace(' && errno != 0', '', 1)):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    check_errno(source.replace(original, changed, 1))
            if name != 'stime_arg0':
                with self.assertRaises(ValueError):
                    check_errno(source.replace(original, original.replace('|| difftm(t, &tm)', ''), 1))

    def test_normal_host_keeps_common_compatibility(self):
        recipe = (ROOT/'tools/touch/Makefile').read_text()
        self.assertIn('HOST_SRCDIR=\tusr.bin/touch', recipe)
        self.assertIn('.include "${.CURDIR}/../Makefile.host"', recipe)
        self.assertNotIn('NOCOMPATLIB', recipe)
        source = (ROOT/'usr.bin/touch/touch.c').read_text()
        self.assertLess(source.index('#include "nbtool_config.h"'), source.index('#include <sys/cdefs.h>'))
        self.assertIn('#include <stddef.h>', source)
        self.assertIn('#include "compat_parsedate.h"', source)
        self.assertIn('#include "compat_filetime.h"', source)
        self.assertIn('#include <util.h>', source)
        makefile = (ROOT/'usr.bin/touch/Makefile').read_text()
        self.assertIn('.if !defined(HOSTPROG)', makefile)
        self.assertIn('LDADD+=\t-lutil', makefile)
        self.assertIn('LDADD+=\t-lm', makefile)


if __name__ == '__main__':
    unittest.main()
