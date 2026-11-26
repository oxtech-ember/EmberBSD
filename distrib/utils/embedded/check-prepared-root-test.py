#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the prepared-root startup checks with flattened mtree entries."""

from pathlib import Path
import runpy
import tempfile
import unittest

check = runpy.run_path(str(Path(__file__).with_name("check-prepared-root.py")))["check"]


class RootTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        (self.root / "bin").mkdir()
        (self.root / "sbin").mkdir()
        self.init = self.root / "sbin/init"
        self.payload = self.root / "bin/console-tools"
        self.payload.write_bytes(b"fixture")
        self.spec = self.root / "root.flat"
        self.entries = {
            "./bin": "type=dir", "./sbin": "type=dir",
            "./bin/console-tools": "type=file mode=0555",
            "./dev/console": "type=char device=0x200 mode=0600",
        }

    def inspect(self):
        self.spec.write_text("".join(f"{path} {info}\n" for path, info in self.entries.items()))
        return check(self.root, self.spec, 0x200)

    def link(self, target):
        self.init.symlink_to(target)
        self.entries["./sbin/init"] = "type=link link=" + target

    def test_regular_init(self):
        self.init.write_bytes(b"fixture")
        self.entries["./sbin/init"] = "type=file mode=0555"
        self.assertEqual(self.inspect(), self.init.resolve())

    def test_link_chain(self):
        self.link("../bin/init-link")
        (self.root / "bin/init-link").symlink_to("/bin/console-tools")
        self.entries["./bin/init-link"] = "type=link link=/bin/console-tools"
        self.assertEqual(self.inspect(), self.payload.resolve())
        del self.entries["./bin/init-link"]
        with self.assertRaises(ValueError):
            self.inspect()

    def test_console_device_and_executable_mode(self):
        self.link("../bin/console-tools")
        self.entries["./dev/console"] = "type=char device=0x4d08 mode=0600"
        with self.assertRaises(ValueError):
            self.inspect()
        self.entries["./dev/console"] = "type=char device=0x200 mode=0600"
        self.entries["./bin/console-tools"] = "type=file mode=0644"
        with self.assertRaises(ValueError):
            self.inspect()

    def test_trailing_directory_component(self):
        for target in ("/bin/console-tools/", "/bin/console-tools/."):
            with self.subTest(target=target):
                self.link(target)
                with self.assertRaises(ValueError):
                    self.inspect()
                self.init.unlink()


if __name__ == "__main__":
    unittest.main()
