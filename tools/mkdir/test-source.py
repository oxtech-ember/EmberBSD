#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Check the fixed invalid-mode input. Do not execute mkdir or C."""
import copy
import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
LEGACY = '111fe0d327e4ccdc7de008ac3844e49893edfbd43d62400ef2dbc4ceb76c5453'


class MkdirSourceTests(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((ROOT / 'tools/mkdir/bootstrap.json').read_text())

    def test_invalid_mode_has_exact_error_and_unchanged_empty_filesystem(self):
        cases = self.manifest['runtime_cases']
        self.assertEqual(len(cases), 11)
        selected = [case for case in cases if case['name'] == 'invalid-mode']
        self.assertEqual(selected, [dict(name='invalid-mode', invocation='mkdir',
            args=['-m', 'invalid', 'dir'], stdin='', exit=1, stdout='',
            stderr="mkdir: Cannot set file mode `invalid'\n",
            filesystem=dict(umask='0022', fixtures={}, expect={}))])

    def test_original_ten_case_contract_is_unchanged(self):
        legacy = copy.deepcopy(self.manifest)
        cases = legacy['runtime_cases']
        self.assertEqual(sum(case['name'] == 'invalid-mode' for case in cases), 1)
        legacy['runtime_cases'] = [case for case in cases if case['name'] != 'invalid-mode']
        raw = (json.dumps(legacy, indent=2) + '\n').encode()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), LEGACY)


if __name__ == '__main__':
    unittest.main()
