#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""The shared console profile selects real native OS build targets."""
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).with_name('static-profile.py')
spec = importlib.util.spec_from_file_location('static_profile', path)
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)

class ConsoleProfile(unittest.TestCase):
    def test_console_profile_has_target_abi_and_minimum_native_components(self):
        selected = profile.console('riscv32')
        self.assertEqual((selected['machine'], selected['arch'], selected['triplet']),
                         ('riscv', 'riscv32', 'riscv32--netbsd'))
        self.assertEqual(selected['headers'], ('lib/libcurses',))
        self.assertEqual(selected['libraries'], ('lib/libterminfo', 'lib/libedit', 'lib/libutil'))
        self.assertEqual(selected['programs'], ('bin/sh', 'sbin/init'))
        self.assertIn('MKSOFTFLOAT=yes', selected['flags'])
        self.assertEqual(selected['elf_class'], 'ELF32')
    def test_foundation_rejects_abi_and_runtime_mismatch(self):
        record = {'format': 'emberbsd-llvm-os-stage', 'phase': 'base',
                  'qualification': 'built-and-inspected',
                  'signature': {'target': {'arch': 'riscv32', 'triplet': 'riscv32--netbsd'},
                                'flags': ['MKSOFTFLOAT=yes', 'MKPIC=no', 'MKPIE=no', 'LDSTATIC=-static'],
                                'runtime': {'manifest': {'identity': 'selected'}}}}
        profile.check_foundation(record, 'riscv32', {'identity': 'selected'})
        for change in ('runtime', 'abi', 'flags'):
            import copy
            invalid = copy.deepcopy(record)
            if change == 'runtime': invalid['signature']['runtime']['manifest'] = {'identity': 'other'}
            if change == 'abi': invalid['signature']['target']['arch'] = 'riscv64'
            if change == 'flags': invalid['signature']['flags'].remove('MKSOFTFLOAT=yes')
            with self.assertRaises(ValueError):
                profile.check_foundation(invalid, 'riscv32', {'identity': 'selected'})

    def test_unimplemented_target_is_rejected(self):
        with self.assertRaises(ValueError):
            profile.console('riscv64')

if __name__ == '__main__':
    unittest.main()
