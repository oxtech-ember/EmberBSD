#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Check the canonical finite sleep contract without executing sleep."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BootstrapSourceTests(unittest.TestCase):
    def manifest(self):
        path = ROOT/'tools/sleep/bootstrap.json'
        self.assertTrue(path.is_file(), 'missing canonical sleep bootstrap contract')
        return json.loads(path.read_text())

    def test_single_canonical_contract_and_normal_source(self):
        value = self.manifest()
        self.assertEqual(value['sources'], ['bin/sleep/sleep.c'])
        self.assertEqual(value['headers'], ['bin/sleep/sleep_darwin.h', 'tools/compat/bootstrap_cdefs.h'])
        self.assertEqual(value['libraries'], ['m'])
        self.assertEqual(value['invocations'], ['sleep'])
        self.assertFalse((ROOT/'tools/sleep/conversion-cases.json').exists())
        self.assertIn('LDADD+= -lm', (ROOT/'bin/sleep/Makefile').read_text())

    def test_real_cli_and_conversion_cases(self):
        cases = self.manifest()['runtime_cases']
        self.assertEqual(len(cases), 21)
        self.assertEqual(sum(c['exit'] == 0 for c in cases), 8)
        self.assertEqual(cases[0]['args'], ['0'])
        self.assertTrue(all(c['stdout'] == '' and 'stderr_nonempty' not in c for c in cases))
        by_args = {tuple(c['args']): c for c in cases}
        for args in ((), ('0','0'), ('',), ('bad',), ('0x',), ('--','-1'),
                     ('nan',), ('NaN',), ('inf',), ('+inf',)):
            self.assertEqual(by_args[args]['exit'], 1)
            self.assertEqual(by_args[args]['stderr'], 'usage: sleep seconds\n')
        for arg in ('1e300', '9223372036854775808.0'):
            self.assertEqual(by_args[(arg,)]['stderr'], 'sleep: requested delay ('+arg+') out of range\n')
        self.assertEqual(by_args[('9223372036854775808',)]['stderr'],
                         'sleep: Requested delay (9223372036854775808) out of range\n')

    def test_one_bounded_outer_command_observation(self):
        timed = [c for c in self.manifest()['runtime_cases'] if 'elapsed' in c]
        self.assertEqual(len(timed), 1)
        self.assertEqual(timed[0]['args'], ['0.5'])
        self.assertEqual(timed[0]['elapsed'], dict(min_ns=500000000, max_ns=5000000000))


if __name__ == '__main__':
    unittest.main()
