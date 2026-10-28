#!/usr/bin/env python3
"""Check source routing without running the historical regression cases."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('runner', Path(__file__).with_name('run.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

class RoutingTest(unittest.TestCase):
    def test_prepared_source_and_missing_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'common').mkdir()
            (root / 'common/delete.c').write_bytes(b'ordinary source\n')
            self.assertEqual(runner.read_nvi_source(root, root, None, 'common/delete.c'), b'ordinary source\n')
            with self.assertRaises(ValueError):
                runner.read_nvi_source(root, None, None, 'common/delete.c')

    def test_revision_reads_historical_git_path(self):
        with patch.object(runner.subprocess, 'check_output', return_value=b'old') as call:
            self.assertEqual(runner.read_nvi_source(Path('/tree'), None, 'abc', 'common/mem.h'), b'old')
        self.assertEqual(call.call_args.args[0][-1], 'abc:external/bsd/nvi/dist/common/mem.h')

if __name__ == '__main__':
    unittest.main()
