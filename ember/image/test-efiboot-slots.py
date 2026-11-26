#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause

import struct
import tempfile
import unittest
from pathlib import Path

from efiboot_slots import initialize_efi


class ImageTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.work = Path(temp.name)
        self.boot = self.work / 'boot'
        self.boot.mkdir()
        self.loader = self.work / 'loader.efi'
        self.profile = {'target': {'arch': 'aarch64'}, 'reset_provider': 'bcm2712-pm',
                        'roots': {'A': 'root-a', 'B': 'root-b'}}
        self.indices = {'A': 2, 'B': 3}
        self.image(0xaa64)

    def image(self, machine):
        data = bytearray(4096)
        struct.pack_into('<2s', data, 0, b'MZ')
        struct.pack_into('<I', data, 60, 128)
        struct.pack_into('<4sH', data, 128, b'PE\0\0', machine)
        struct.pack_into('<H', data, 148, 240)
        struct.pack_into('<H', data, 152, 0x20b)
        struct.pack_into('<H', data, 220, 10)
        self.loader.write_bytes(data)

    def initialize(self, *options):
        return initialize_efi(self.boot, self.loader, self.profile, indices=self.indices,
                              extra_args=options)

    def test_arm64_factory(self):
        directory = self.initialize('console=plcom0')
        self.assertEqual(directory, self.boot / 'EFI/EmberBSD')
        self.assertEqual((directory / 'reset-provider').read_bytes(), b'bcm2712-pm')
        self.assertEqual((self.boot / 'EFI/BOOT/bootaa64.efi').read_bytes(), self.loader.read_bytes())
        for slot, part in [('a', 'b'), ('b', 'c')]:
            self.assertIn(f'boot bootpart{part}:netbsd console=plcom0 root=NAME=root-{slot}\n',
                          (directory / f'boot-{slot}.cfg').read_text())
        self.assertEqual({p.name for p in directory.iterdir()}, {'boot-a.cfg', 'boot-b.cfg', 'reset-provider'})
        (directory / 'osnext.dat').write_bytes(b'B')
        self.initialize('console=plcom0')
        self.assertEqual((directory / 'osnext.dat').read_bytes(), b'B')

    def test_x86_uses_partition_from_the_image_disk(self):
        self.profile['target']['arch'] = 'x86_64'
        self.profile['reset_provider'] = 'none'
        self.image(0x8664)
        directory = self.initialize()
        self.assertIn('boot bootpartc:netbsd\n', (directory / 'boot-b.cfg').read_text())
        self.assertTrue((self.boot / 'EFI/BOOT/bootx64.efi').exists())

    def test_invalid_data_preserves_the_previous_files(self):
        for change in ('machine', 'duplicate partition', 'args', 'label', 'provider'):
            with self.subTest(change=change):
                self.setUp()
                flags = []
                if change == 'machine':
                    self.image(0x8664)
                elif change == 'duplicate partition':
                    self.indices['B'] = self.indices['A']
                elif change == 'args':
                    flags = ['console=plcom0\nmenu=Broken:prompt']
                elif change == 'label':
                    self.profile['roots']['A'] = 'root-a\n'
                else:
                    self.profile['reset_provider'] = 'bcm2712-pm\n'
                with self.assertRaises(ValueError):
                    self.initialize(*flags)
                self.assertEqual(list(self.boot.iterdir()), [])

    def test_linked_stage_does_not_modify_the_other_directory(self):
        other = self.work / 'other'
        other.mkdir()
        (self.boot / 'EFI').symlink_to(other, target_is_directory=True)
        with self.assertRaises(ValueError):
            self.initialize()
        self.assertEqual(list(other.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
