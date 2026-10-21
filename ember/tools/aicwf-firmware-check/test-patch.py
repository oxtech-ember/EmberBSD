#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; finite patch-input and desired-observation controls.
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import patching
import patched
import observations
import driver
from unittest.mock import patch


def elf_fixture():
    names = b'\0.aicwf_bip_key\0.aicwf_bip_rx\0.shstrtab\0'
    data = bytearray(1052)
    data[:16] = b'\x7fELF\x01\x01\x01' + bytes(9)
    struct.pack_into('<HHIIIIIHHHHHH', data, 16, 2, 40, 1, 0x142be1,
                     52, 892, 0x05000200, 52, 32, 2, 40, 4, 3)
    for index, offset, address, size in ((0, 128, 0x142be0, 340), (1, 468, 0x165e48, 384)):
        struct.pack_into('<8I', data, 52 + index * 32, 1, offset, address, address, size, size, 5, 4)
        struct.pack_into('<10I', data, 932 + index * 40, 1 if index == 0 else 16, 1, 6, address, offset, size, 0, 0, 4, 0)
    data[852:852 + len(names)] = names
    struct.pack_into('<10I', data, 1012, 30, 3, 0, 0, 852, len(names), 0, 0, 4, 0)
    return data


class PatchInputs(unittest.TestCase):
    def test_exact_two_rx_windows(self):
        self.assertEqual([len(value) for value in patching.parse_elf(elf_fixture()).values()], [340, 384])

    def test_reject_slot_length_flags_and_unknown_alloc(self):
        for offset, value in ((52 + 8, 0x142be2), (52 + 16, 341), (52 + 24, 7), (932 + 8, 3), (1012 + 8, 2)):
            with self.subTest(offset=offset):
                data = elf_fixture(); struct.pack_into('<I', data, offset, value)
                self.assertRaises(ValueError, patching.parse_elf, data)

    def test_reject_relocation_and_bad_tables(self):
        for offset, value in ((1012 + 4, 4), (28, 0xfffffff0), (32, 0xfffffff0), (44, 3)):
            data = elf_fixture(); struct.pack_into('<I' if offset != 44 else '<H', data, offset, value)
            self.assertRaises(ValueError, patching.parse_elf, data)
        self.assertRaises(ValueError, patching.parse_elf, b'ELF')

    def test_unknown_identity_and_duplicate_manifest(self):
        with tempfile.TemporaryDirectory(prefix='aic-patch-') as temporary:
            root = Path(temporary); elf = root / 'unknown.elf'; manifest = root / 'manifest.json'
            elf.write_bytes(elf_fixture()); manifest.write_text('{}')
            self.assertRaisesRegex(ValueError, 'identity', patching.load, elf, manifest)
            manifest.write_text('{"schema":1,"schema":2}')
            self.assertRaisesRegex(ValueError, 'Duplicate', patching.json_document, manifest.read_bytes())


class DesiredObservations(unittest.TestCase):
    def raw(self, row, after, returned=0, special=0xa5, frame=None):
        before = patched.before_state(row)
        raw = struct.pack('<7I', 0, returned, 0, row['mask'], 0, 0, special) + before + after
        frame = row['frame'] if frame is None else frame
        return raw, frame + bytes(patched.FRAME_BYTES - len(frame))

    def test_same_key_refresh_and_precise_v1_refusal(self):
        row = patched.make('same-hw-causal', 4, floor=100, initial=99, tx=7, hwindex=7, incoming_hwindex=9, igtk=2)
        before = patched.before_state(row); c = patched.context(row)
        self.assertEqual(before[c + 0x62], 7)
        after = bytearray(before); after[c + 0x62] = 9
        raw, frame = self.raw(row, bytes(after))
        self.assertTrue(patched.compare([row], raw, frame)['accepted_patch_fixture'])
        raw, frame = self.raw(row, before)
        self.assertRaisesRegex(ValueError, 'HW index mismatch: same-hw-causal', patched.compare, [row], raw, frame)

    def test_same_key_max_preserves_tx_key_and_pointer(self):
        for initial in (99, 100, 101):
            row = patched.make('same-max', 4, floor=100, initial=initial, tx=7, hwindex=7, incoming_hwindex=9, igtk=2)
            before = patched.before_state(row); c = patched.context(row)
            after = bytearray(before); after[c:c + 8] = max(100, initial).to_bytes(8, 'little'); after[c + 0x62] = 9
            raw, frame = self.raw(row, bytes(after))
            self.assertTrue(patched.compare([row], raw, frame)['accepted_patch_fixture'])
            for offset in (c + 0x48, c + 0x50, c + 0x64, 0x4b4):
                damaged = bytearray(after); damaged[offset] ^= 1
                corrupt, frame = self.raw(row, bytes(damaged))
                self.assertRaisesRegex(ValueError, 'complete state', patched.compare, [row], corrupt, frame)

    def test_full48_rx_and_bad_mic_no_commit(self):
        for bad in (False, True):
            row = patched.make('rx-carry', floor=0xffffffff, pn=0x100000000, bad=bad)
            before = patched.before_state(row); c = patched.context(row)
            after = bytearray(before)
            if not bad:after[c:c + 8] = (0x100000000).to_bytes(8, 'little')
            frame = row['frame'][:-8] + bytes(8)
            raw, frames = self.raw(row, bytes(after), int(bad), 0 if bad else 0xa5, frame)
            self.assertTrue(patched.compare([row], raw, frames)['accepted_patch_fixture'])
            after[c:c + 8] = (2).to_bytes(8, 'little')
            raw, frames = self.raw(row, bytes(after), int(bad), 0 if bad else 0xa5, frame)
            self.assertRaisesRegex(ValueError, 'complete state', patched.compare, [row], raw, frames)

    def test_abi_mask_before_seed_and_mpdu_corruptions_refuse(self):
        row = patched.make('rx-replay', floor=2, pn=2)
        raw, frame = self.raw(row, patched.before_state(row), 1, 0)
        self.assertTrue(patched.compare([row], raw, frame)['accepted_patch_fixture'])
        for offset in (0, 12, 16, 28):
            changed = bytearray(raw); changed[offset] ^= 1
            self.assertRaises(ValueError, patched.compare, [row], bytes(changed), frame)
        changed = bytearray(frame); changed[-patched.FRAME_BYTES + 1] ^= 1
        self.assertRaisesRegex(ValueError, 'MPDU', patched.compare, [row], raw, bytes(changed))

    def test_unknown_patch_and_unpaired_option_before_output(self):
        with tempfile.TemporaryDirectory(prefix='aic-guard-') as temporary:
            root = Path(temporary); elf = root / 'unknown.elf'; manifest = root / 'manifest.json'; output = root / 'output'
            elf.write_bytes(elf_fixture()); manifest.write_text('{}')
            common = ['--firmware', str(root / 'not-read.bin'), '--output', str(output), '--patch-elf', str(elf)]
            for args in (common, common + ['--patch-manifest', str(manifest)]):
                with patch.object(driver, 'sdk', side_effect=AssertionError('SDK must not run')):
                    self.assertRaises(ValueError, driver.main, args)
                self.assertFalse(output.exists())

    def test_finite_matrix_and_irq_cases_are_not_coverage_claims(self):
        rows = patched.inputs(); irq = patched.inputs(irq=True)
        self.assertEqual(len(rows), 146); self.assertEqual(len(irq), 36)
        self.assertEqual({row['mask'] for row in rows}, {0, 1})
        self.assertEqual({row['irq_phase'] for row in irq}, {'before', 'during', 'after'})
        self.assertTrue(all(len(row['frame']) <= patched.FRAME_BYTES for row in rows + irq))


if __name__ == '__main__':
    unittest.main(verbosity=2)
