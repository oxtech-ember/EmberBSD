#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Check conversion guards and future native inputs. Never execute C."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ConversionSourceTests(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / 'bin/sleep/sleep.c').read_text()

    def test_nonfinite_is_rejected_before_floor_and_cast(self):
        self.assertIn('!isfinite(val)', self.source)
        self.assertLess(self.source.index('!isfinite(val)'), self.source.index('ival = floor(val);'))
        self.assertLess(self.source.index('!isfinite(val)'), self.source.index('ntime.tv_sec = ival;'))

    def test_type_sized_bound_is_checked_before_conversion(self):
        guard = 'ival >= ldexp(1.0, (int)(sizeof(time_t) * CHAR_BIT) -'
        self.assertIn(guard, self.source)
        self.assertIn('((time_t)-1 < 0))', self.source)
        self.assertLess(self.source.index(guard), self.source.index('ntime.tv_sec = ival;'))
        self.assertIn('if ((double)ntime.tv_sec != ival)', self.source)
        self.assertIn('ntime.tv_sec = strtol(arg, &temp, 10);', self.source)

    def test_bounded_native_inputs_cover_invalid_and_valid_conversions(self):
        data = json.loads((ROOT / 'tools/sleep/bootstrap.json').read_text())
        cases = {case['args'][0]: case for case in data['runtime_cases'] if len(case['args']) == 1}
        self.assertEqual(len(data['runtime_cases']), 21)
        for text in ('nan', 'NaN', 'inf', '+inf', '1e300', '9223372036854775808.0'):
            self.assertEqual(cases[text]['exit'], 1)
        for text in ('0', '0.0', '0.001', '1e-9999', '1e-3', '0x0.1'):
            self.assertEqual(cases[text]['exit'], 0)
            self.assertEqual(cases[text]['stderr'], '')


if __name__ == '__main__':
    unittest.main()
