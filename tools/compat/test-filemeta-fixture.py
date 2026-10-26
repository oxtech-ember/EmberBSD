#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Source controls for fixed native fixture operations; no native children."""
import json
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parent

class FixtureContract(unittest.TestCase):
    def test_native_fixture_contract_exists(self):
        self.assertTrue((ROOT / 'filemeta-fixture.json').is_file())

    def test_shared_support_has_one_definition(self):
        support = ROOT / 'probes/filemeta-fixture-support.h'
        self.assertTrue(support.is_file())
        for name in ('filemeta-check.c', 'filemeta-fixture.c'):
            text = (ROOT / 'probes' / name).read_text()
            self.assertIn('#include "filemeta-fixture-support.h"', text)
            self.assertNotIn('make_acl(int inherit)', text)

    def test_component_manifest_retains_the_shared_check_header(self):
        manifest = json.loads((ROOT / 'bootstrap-filemeta.json').read_bytes())
        self.assertIn('tools/compat/probes/filemeta-fixture-support.h',
                      manifest['headers'])

    def test_closed_native_operations_have_no_copy_or_shell(self):
        path = ROOT / 'probes/filemeta-fixture.c'
        self.assertTrue(path.is_file())
        text = path.read_text()
        self.assertIn('argc != 3', text)
        self.assertIn('strcmp(argv[1], "prepare")', text)
        self.assertIn('strcmp(argv[1], "observe")', text)
        for forbidden in ('__nbcompat_copy_', 'system(', 'exec', 'popen(', 'fopen(', 'argv[3]'):
            self.assertNotIn(forbidden, text)
        self.assertIn('O_CREAT | O_EXCL', text)
        self.assertIn('XATTR_NOFOLLOW', (ROOT / 'probes/filemeta-fixture-support.h').read_text())

    def test_manifest_names_four_independent_operations(self):
        path = ROOT / 'filemeta-fixture.json'
        self.assertTrue(path.is_file())
        manifest = json.loads(path.read_bytes())
        self.assertEqual(manifest['operations'], ['prepare', 'observe'])
        self.assertEqual(manifest['cases'], ['file-replace', 'file-overlay', 'empty-acl-overlay', 'directory-inherit'])
        self.assertEqual(manifest['paths'], ['src', 'dst'])
        self.assertEqual(manifest['source'], 'tools/compat/probes/filemeta-fixture.c')
        self.assertEqual(manifest['headers'], ['tools/compat/probes/filemeta-fixture-support.h'])
        self.assertEqual(manifest['host'], 'Darwin')


class CopyBehaviorRunner(unittest.TestCase):
    def setUp(self):
        path = ROOT / 'test-copy-metadata.py'
        self.assertTrue(path.is_file(), 'the normal metadata behavior runner is missing')
        spec = importlib.util.spec_from_file_location('copy_metadata_test', path)
        self.runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.runner)

    def test_directory_copy_targets_the_existing_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            calls = []

            def invoke(argv, **kwargs):
                calls.append(argv)
                cwd = kwargs['cwd']
                if argv[1] == 'prepare':
                    (cwd / 'src').mkdir()
                    (cwd / 'dst').mkdir()
                code = 1 if len(calls) == 2 else 0
                return subprocess.CompletedProcess(argv, code, b'', b'mismatch\n' if code else b'')

            with patch.object(self.runner.subprocess, 'run', side_effect=invoke):
                rows = self.runner.run_case(Path('/selected/fixture'), Path('/selected/cp'),
                                            work, 'directory-inherit')
            self.assertEqual(calls[2], ['/selected/cp', '-Rp', 'src/.', 'dst'])
            self.assertEqual([row['actual_exit'] for row in rows], [0, 1, 0, 0])

    def test_an_observer_that_accepts_the_uncopied_destination_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(self.runner.subprocess, 'run',
                              return_value=subprocess.CompletedProcess([], 0, b'', b'')) as run:
                with self.assertRaisesRegex(RuntimeError, 'before-copy'):
                    self.runner.run_case(Path('/selected/fixture'), Path('/selected/cp'),
                                         Path(temporary), 'file-replace')
            self.assertEqual(run.call_count, 2)

    def test_failed_setup_never_invokes_cp(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(self.runner.subprocess, 'run',
                              return_value=subprocess.CompletedProcess([], 1, b'', b'failed')) as run:
                with self.assertRaisesRegex(RuntimeError, 'prepare'):
                    self.runner.run_case(Path('/selected/fixture'), Path('/selected/cp'),
                                         Path(temporary), 'file-overlay')
            self.assertEqual(run.call_count, 1)

    def test_native_children_receive_explicit_sanitizer_options(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1:symbolize=0',
                            UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=0'):
                with patch.object(self.runner.subprocess, 'run',
                                  return_value=subprocess.CompletedProcess([], 1, b'', b'failed')) as run:
                    with self.assertRaises(RuntimeError):
                        self.runner.run_case(Path('/selected/fixture'), Path('/selected/cp'),
                                             Path(temporary), 'file-overlay')
                    environment = run.call_args.kwargs['env']
                    self.assertEqual(environment['ASAN_OPTIONS'], os.environ['ASAN_OPTIONS'])
                    self.assertEqual(environment['UBSAN_OPTIONS'], os.environ['UBSAN_OPTIONS'])

if __name__ == '__main__':
    unittest.main()
