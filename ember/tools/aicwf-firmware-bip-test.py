#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: AI-assisted checks of the fixed-image patch producer.
"""Check fixed ELF input refusal without running ARM instructions."""

import importlib.util
from pathlib import Path
import struct
import sys
import unittest

SPEC = importlib.util.spec_from_file_location(
    'aicwf_bip', Path(__file__).with_name('aicwf-firmware-bip.py'))
BIP = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BIP)


def fixture():
    names = b'\0.aicwf_bip_key\0.aicwf_bip_rx\0.shstrtab\0'
    key_offset, rx_offset, names_offset, table_offset = 128, 468, 852, 892
    data = bytearray(table_offset + 160)
    data[:16] = b'\x7fELF\x01\x01\x01' + bytes(9)
    struct.pack_into('<HHIIIIIHHHHHH', data, 16, 2, 40, 1, 0x142be1,
                     52, table_offset, 0x05000000, 52, 32, 2, 40, 4, 3)
    struct.pack_into('<IIIIIIII', data, 52, 1, key_offset, 0x142be0,
                     0x142be0, 340, 340, 5, 4)
    struct.pack_into('<IIIIIIII', data, 84, 1, rx_offset, 0x165e48,
                     0x165e48, 384, 384, 5, 4)
    data[names_offset:names_offset + len(names)] = names
    for index, name, address, offset, size, flags, kind in (
            (1, 1, 0x142be0, key_offset, 340, 6, 1),
            (2, 16, 0x165e48, rx_offset, 384, 6, 1),
            (3, 30, 0, names_offset, len(names), 0, 3)):
        struct.pack_into('<IIIIIIIIII', data, table_offset + 40 * index,
                         name, kind, flags, address, offset, size, 0, 0, 4, 0)
    return data


class PatchContract(unittest.TestCase):
    def test_exact_two_windows(self):
        rows = BIP.patch_sections(bytes(fixture()))
        self.assertEqual([(r['address'], len(r['data'])) for r in rows],
                         [(0x142be0, 340), (0x165e48, 384)])

    def test_kernel_header_has_only_replacements_and_digests(self):
        rows = BIP.patch_sections(bytes(fixture()))
        header = BIP.kernel_header(rows, '11' * 32, {'aicwf-firmware-bip.S': '22' * 32})
        self.assertIn(b'aicwf_fw_bip_key[340]', header)
        self.assertIn(b'aicwf_fw_bip_rx[384]', header)
        self.assertIn(b'aicwf_fw_bip_modified_sha256[32]', header)
        self.assertNotIn(b'original_image[', header)
        self.assertIn(b'BSD-2-Clause', header)
        self.assertIn(b'22' * 32, header)

    def test_unsupported_original(self):
        for data in (b'', bytes(341200), bytes(341768)):
            with self.assertRaises(ValueError):
                BIP.check_original(data)

    def test_same_version_is_not_image_identity(self):
        data = bytearray(341200)
        struct.pack_into('<I', data, 0x1c, 0x06090101)
        with self.assertRaises(ValueError):
            BIP.check_original(bytes(data))

    def test_reject_implicit_elf_header_load(self):
        data = fixture()
        struct.pack_into('<IIIIIIII', data, 52, 1, 0, 0x142000,
                         0x142000, 0xd34, 0xd34, 5, 4096)
        with self.assertRaises(ValueError):
            BIP.patch_sections(data)

    def test_reject_wrong_machine(self):
        data = fixture()
        struct.pack_into('<H', data, 18, 183)
        with self.assertRaises(ValueError):
            BIP.patch_sections(data)

    def test_reject_wrong_entry(self):
        data = fixture()
        struct.pack_into('<I', data, 24, 0x142be0)
        with self.assertRaises(ValueError):
            BIP.patch_sections(data)

    def test_reject_writable_segment(self):
        data = fixture()
        struct.pack_into('<I', data, 52 + 24, 7)
        with self.assertRaises(ValueError):
            BIP.patch_sections(data)

    def test_reject_grown_or_shifted_window(self):
        for offset, value in ((892 + 40 + 20, 341),
                              (892 + 40 + 12, 0x142be2),
                              (892 + 80 + 20, 383)):
            data = fixture()
            struct.pack_into('<I', data, offset, value)
            with self.assertRaises(ValueError):
                BIP.patch_sections(data)

    def test_reject_missing_or_extra_allocated_section(self):
        for offset, value in ((892 + 80 + 8, 0), (892 + 120 + 8, 2)):
            data = fixture()
            struct.pack_into('<I', data, offset, value)
            with self.assertRaises(ValueError):
                BIP.patch_sections(data)

    def test_reject_truncated_or_outside_file(self):
        for data in (bytes(fixture()[:900]), bytes(fixture()[:51])):
            with self.assertRaises(ValueError):
                BIP.patch_sections(data)
        data = fixture()
        struct.pack_into('<I', data, 892 + 40 + 16, 0xfffffff0)
        with self.assertRaises(ValueError):
            BIP.patch_sections(data)


if __name__ == '__main__':
    unittest.main()
