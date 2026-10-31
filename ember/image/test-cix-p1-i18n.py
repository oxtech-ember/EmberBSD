#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('cix_i18n', Path(__file__).with_name('cix-p1-i18n.py'))
i18n = importlib.util.module_from_spec(spec)
spec.loader.exec_module(i18n)
REV = 'a' * 40


class InventoryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.cohort = Path(self.temp.name) / 'cohort'
        self.dest = self.cohort / 'dest'
        self.root = Path(self.temp.name) / 'root'
        self.root.mkdir()
        for name in i18n.REQUIRED:
            path = self.dest / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'data\n')
        self.metalog = self.dest / 'METALOG'
        lines = []
        for path in sorted(self.dest.rglob('*')):
            name = path.relative_to(self.dest).as_posix()
            kind = 'dir' if path.is_dir() else 'file'
            lines.append(f'./{name} type={kind} uname=root gname=wheel mode=0755\n')
        self.metalog.write_text(''.join(lines))

    def record(self):
        i18n.main(['record', str(self.cohort), REV])
        inventory = self.cohort / i18n.INVENTORY
        receipt = self.cohort / 'receipt'
        receipt.write_text('i18n-inventory-sha256=' + hashlib.sha256(inventory.read_bytes()).hexdigest() + '\n')
        return inventory, receipt

    def test_round_trip_and_mutation(self):
        _, receipt = self.record()
        inventory = i18n.verified(self.cohort, REV, receipt)
        i18n.copy(self.cohort, self.root, inventory)
        i18n.check_copy(self.root, inventory)
        (self.root / i18n.REQUIRED[0]).write_text('changed')
        with self.assertRaisesRegex(ValueError, 'Changed staged DATA bytes'):
            i18n.check_copy(self.root, inventory)

    def test_staged_data_root_cannot_be_symlink(self):
        _, receipt = self.record()
        inventory = i18n.verified(self.cohort, REV, receipt)
        i18n.copy(self.cohort, self.root, inventory)
        root = self.root / 'usr/share/i18n'
        saved = root.with_name('saved')
        root.rename(saved)
        root.symlink_to(saved, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'Invalid staged DATA file type'):
            i18n.check_copy(self.root, inventory)

    def test_duplicate_json_and_receipt_mutation(self):
        inventory, receipt = self.record()
        text = inventory.read_text().replace('"format": 1', '"format": 1, "format": 1')
        inventory.write_text(text)
        receipt.write_text('i18n-inventory-sha256=' + hashlib.sha256(inventory.read_bytes()).hexdigest() + '\n')
        with self.assertRaisesRegex(ValueError, 'Duplicate JSON key'):
            i18n.verified(self.cohort, REV, receipt)
        receipt.write_text(receipt.read_text().rstrip('\n'))
        with self.assertRaisesRegex(ValueError, 'receipt hash'):
            i18n.verified(self.cohort, REV, receipt)

    def test_missing_symlink_extra_and_unsafe_metadata(self):
        target = self.dest / i18n.REQUIRED[0]
        target.unlink()
        with self.assertRaisesRegex(ValueError, 'Required native DATA missing'):
            i18n.snapshot(self.cohort, REV)
        target.symlink_to(self.dest / i18n.REQUIRED[1])
        with self.assertRaisesRegex(ValueError, 'Unsupported DATA path or file type'):
            i18n.snapshot(self.cohort, REV)
        target.unlink()
        target.write_text('restored')
        (target.parent / 'extra').write_text('extra')
        with self.assertRaisesRegex(ValueError, 'Unsupported DATA path or file type'):
            i18n.snapshot(self.cohort, REV)
        (target.parent / 'extra').unlink()
        self.metalog.write_text(self.metalog.read_text().replace('type=file', 'type=file content=/escape', 1))
        with self.assertRaisesRegex(ValueError, 'Unsupported native DATA metadata'):
            i18n.snapshot(self.cohort, REV)

    def test_json_type_is_not_coerced(self):
        inventory, receipt = self.record()
        data = json.loads(inventory.read_text())
        data['format'] = True
        inventory.write_text(json.dumps(data))
        receipt.write_text('i18n-inventory-sha256=' + hashlib.sha256(inventory.read_bytes()).hexdigest() + '\n')
        with self.assertRaisesRegex(ValueError, 'inventory differs'):
            i18n.verified(self.cohort, REV, receipt)


if __name__ == '__main__':
    unittest.main()
