#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise static network prepared-source object state and retry behavior."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest


sdk_source = os.environ.get('EMBERBSD_SDK_SOURCE')
if sdk_source:
    sys.path.insert(0, str(Path(sdk_source) / 'src'))
from emberbsd_sdk import prepared_refresh

refresh_path = Path(__file__).with_name('network-source-refresh.py')
spec = importlib.util.spec_from_file_location('network_source_refresh', refresh_path)
network_refresh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(network_refresh)


class NetworkSourceRefreshTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name).resolve()
        self.source = root / 'os'
        self.output = root / 'out'
        (self.source / 'share/mk').mkdir(parents=True)
        (self.source / 'external/cbor').mkdir(parents=True)
        (self.source / 'external/cbor/Makefile').write_text('')
        self.output.mkdir()
        self.obj = self.output / 'obj'
        self.crunch = self.output / 'crunch'
        self.obj.mkdir()
        self.crunch.mkdir()
        contract = {
            'version': 1,
            'sources': {'EMBER_LIBCBOR_SOURCE': {
                'producer_dirs': ['external/cbor'],
                'consumer_dirs': [], 'header_prefixes': [], 'linked_names': []}},
            'header_independent_object_patterns': [],
            'header_independent_archive_patterns': []}
        (self.source / prepared_refresh.CONTRACT).write_text(json.dumps(contract))
        self.old_record = 'a' * 64
        self.current_record = 'b' * 64
        self.current_path = str(root / 'prepared/current')
        self.identity = {'libcbor': {'record': self.current_record,
                                     'path': self.current_path}}
        (self.output / 'BUILD-INPUTS.txt').write_text(
            'libcbor-source-record-sha256={}\n'.format(self.old_record))
        self.write_objects()

    def write_objects(self):
        for directory in (self.obj / 'external/cbor', self.crunch / 'cbor'):
            directory.mkdir(parents=True, exist_ok=True)
            (directory / 'cbor.d').write_text('cbor.o: /old/prepared/cbor.c\n')
            (directory / 'cbor.o').write_text('stale object\n')

    def test_matching_object_source_record_preserves_objects(self):
        (self.output / 'BUILD-INPUTS.txt').write_text(
            'libcbor-source-record-sha256={}\n'.format(self.current_record))
        (self.output / network_refresh.STATE).write_text(json.dumps(
            {'version': 1, 'sources': self.identity}))
        cleaned = []

        def clean_native(directory):
            cleaned.append(directory)
            shutil.rmtree(self.obj / directory)

        before = [(path.stat().st_ino, path.stat().st_mtime_ns)
                  for path in (self.obj / 'external/cbor/cbor.o',
                               self.crunch / 'cbor/cbor.o')]
        changed = network_refresh.refresh(
            self.source, self.output, self.identity,
            {'cbor': 'external/cbor'}, prepared_refresh, clean_native)

        self.assertEqual(changed, [])
        self.assertEqual(cleaned, [])
        state = json.loads((self.output / network_refresh.STATE).read_text())
        self.assertEqual(state, {'version': 1, 'sources': self.identity})
        after = [(path.stat().st_ino, path.stat().st_mtime_ns)
                 for path in (self.obj / 'external/cbor/cbor.o',
                              self.crunch / 'cbor/cbor.o')]
        self.assertEqual(after, before)

    def test_changed_source_cleans_scopes_once_and_failed_build_retry_reuses(self):
        # The completed-root receipt may carry the new source hash even though
        # older object metadata still refers to an old absolute source path.
        (self.output / 'BUILD-INPUTS.txt').write_text(
            'libcbor-source-record-sha256={}\n'.format(self.current_record))
        root_receipt = (self.output / 'BUILD-INPUTS.txt').read_text()
        cleaned = []
        state_path = self.output / network_refresh.STATE
        state_before = None

        def clean_native(directory):
            current = state_path.read_bytes() if state_path.exists() else None
            self.assertEqual(current, state_before)
            cleaned.append(directory)
            shutil.rmtree(self.obj / directory)

        changed = network_refresh.refresh(
            self.source, self.output, self.identity,
            {'cbor': 'external/cbor'}, prepared_refresh, clean_native)
        self.assertEqual(changed, ['libcbor'])
        self.assertEqual(cleaned, ['external/cbor'])
        self.assertFalse((self.crunch / 'cbor').exists())
        self.assertEqual((self.output / 'BUILD-INPUTS.txt').read_text(), root_receipt)
        state = json.loads(state_path.read_text())
        self.assertEqual(state, {'version': 1, 'sources': self.identity})

        # Model a failed compile after the scoped cleanup/state commit. A retry
        # with the same inputs must reuse compatible partial outputs.
        self.write_objects()
        object_identities = [(path.stat().st_ino, path.stat().st_mtime_ns)
                             for path in (self.obj / 'external/cbor/cbor.o',
                                          self.crunch / 'cbor/cbor.o')]
        retried = network_refresh.refresh(
            self.source, self.output, self.identity,
            {'cbor': 'external/cbor'}, prepared_refresh, clean_native)
        self.assertEqual(retried, [])
        self.assertEqual(cleaned, ['external/cbor'])
        self.assertEqual([(path.stat().st_ino, path.stat().st_mtime_ns)
                          for path in (self.obj / 'external/cbor/cbor.o',
                                       self.crunch / 'cbor/cbor.o')],
                         object_identities)

        switched = {'libcbor': {'record': self.current_record,
                                'path': str(Path(self.current_path).parent / 'alternate')}}
        state_before = state_path.read_bytes()
        switched_names = network_refresh.refresh(
            self.source, self.output, switched,
            {'cbor': 'external/cbor'}, prepared_refresh, clean_native)
        self.assertEqual(switched_names, ['libcbor'])
        self.assertEqual(cleaned, ['external/cbor', 'external/cbor'])


if __name__ == '__main__':
    unittest.main()
