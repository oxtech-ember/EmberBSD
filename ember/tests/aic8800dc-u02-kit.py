#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check two factory releases without redistributing their binary files."""
from itertools import product
from pathlib import Path
import os
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / "tools/aic8800dc-u02-kit.py"
sys.path.insert(0, str(TOOL.parent))
kit = runpy.run_path(str(TOOL))
tables = runpy.run_path(str(TOOL.with_name("aic8800dc-u02-tables.py")))
# Independent measurements from the two unmodified factory releases.
MODULES = (
    "df86e12cd99327d776988ee792378f7fd8d82097c5fe3b57dc82bca785ae0693",
    "67c7324a1bb37a0c440ae4d4ad73f8451e39ffe6005a80bc892c6a7dd7159e73",
)
FILES = {
    "aic8800dc_u02_tables.bin": (
        (4836, "7e934020dd15a26c162628132e59db8746ec9e23ef2086027071303a16b97025"),
        (4836, "7e934020dd15a26c162628132e59db8746ec9e23ef2086027071303a16b97025"),
    ),
    "aic_powerlimit_8800dc.txt": (
        (395, "98cc941ca3d35d035ef3a2ac448e5059153418ff9399b89894cf12a2077dfbac"),
        (3302, "bc8a5c48dba35faa5f4955212aa98ba65d501511740512a632f12b38c4f6300d"),
    ),
    "aic_userconfig_8800dc.txt": (
        (1094, "0ceb4022f4fa8cb5c2b511330b92464aaa19e4ab05bb6fbf3f9a46f6cddc10d0"),
        (1135, "a1ff99eb234ea886413f79126e204f729f4ef281397b3fe8056beaec31d9f624"),
    ),
    "fmacfw_calib_8800dc_u02.bin": (
        (40284, "43be8bd7c57489bbf7a9da711bcb9de05f43503c5818868ae2b7402ef80502bf"),
        (40284, "db3c90ba2336f71b87f2e2b92e71b6b395422e146e64e6863013d553baa90b48"),
    ),
    "fmacfw_patch_8800dc_u02.bin": (
        (30936, "31df3397e9cba073bcef31b1b95fbfb83dfdfcaae3efa63453cc3e093f895a4f"),
        (31624, "69d3ac2038da3b8e652ed1ec5079598ceb6df51db7b87b1d33f6d3c820c86a6f"),
    ),
    "fmacfw_patch_tbl_8800dc_u02.bin": (
        (896, "41c21a27f609a9c7a2194bdf1689d200c3076efa986c3ec38da5ba363eec25b8"),
        (1008, "62d53a223eda1ea064ba82a6fe67829d0720e9f4e87d26763fd13316ccd2a90b"),
    ),
}


class FactoryTables(unittest.TestCase):
    def test_section_order_and_sizes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for number, (name, size) in enumerate(tables["TABLES"]):
                (root / name).write_bytes(bytes([number]) * size)
            result = tables["pack_tables"](root)
            self.assertEqual(len(result), 4836)
            self.assertEqual(struct.unpack_from("<8sHH", result), (b"AICDCU02", 1, 11))
            offset = 12
            for number, size in enumerate((16, 16, 192, 16, 1340, 2048, 384, 128, 128, 256, 256)):
                self.assertEqual(struct.unpack_from("<I", result, offset)[0], size)
                offset += 4
                self.assertEqual(result[offset:offset + size], bytes([number]) * size)
                offset += size
            self.assertEqual(offset, len(result))

            (root / "agc_cfg_ram").write_bytes(b"")
            with self.assertRaisesRegex(ValueError, "agc_cfg_ram"):
                tables["pack_tables"](root)

    def test_unknown_source_preserves_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "existing"
            output.write_bytes(b"previous result")
            (root / "aicwf_compat_8800dc.c").write_bytes(bytes(80074))
            with self.assertRaisesRegex(ValueError, "source differs"):
                tables["prepare"](root, root, output)
            self.assertEqual(output.read_bytes(), b"previous result")


    def test_special_files_and_source_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = root / "existing"
            output.write_bytes(b"previous result")
            path = root / "aicwf_compat_8800dc.c"
            producer = TOOL.with_name("aic8800dc-u02-tables.py")
            for kind, message in (("fifo", "regular file"), ("symlink", "regular file"),
                                  ("oversize", "exactly 80074"), ("short", "exactly 80074")):
                with self.subTest(kind=kind):
                    if kind == "fifo":
                        os.mkfifo(path)
                    elif kind == "symlink":
                        path.symlink_to(output)
                    else:
                        with path.open("wb") as source:
                            source.truncate(80075 if kind == "oversize" else 80073)
                    result = subprocess.run(
                        [sys.executable, producer, "--bsp", root, "--sdk-bin", root, output],
                        capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 1)
                    self.assertIn(message, result.stderr)
                    self.assertEqual(output.read_bytes(), b"previous result")
                    path.unlink()


class FactoryKit(unittest.TestCase):
    def test_every_release_combination(self):
        self.assertEqual(set(FILES), set(kit["FILES"]))
        different = {name for name, pair in FILES.items() if pair[0] != pair[1]}
        for module_index, driver in enumerate(MODULES):
            for bits in product((0, 1), repeat=len(different)):
                choices = dict(zip(sorted(different), bits))
                values = {n: pair[choices.get(n, 0)] for n, pair in FILES.items()}
                assets = {n: (size, bytes.fromhex(digest)) for n, (size, digest) in values.items()}
                with self.subTest(module=module_index, selection=bits):
                    if all(bit == module_index for bit in bits):
                        self.assertEqual(kit["identify"](driver, assets)[0],
                                         ("g0a86431", "261020 / g09cea8d")[module_index])
                    else:
                        with self.assertRaises(ValueError):
                            kit["identify"](driver, assets)

    def test_missing_or_unrecognized(self):
        with self.assertRaisesRegex(ValueError, "incomplete"):
            kit["identify"](MODULES[0], {})
        with self.assertRaisesRegex(ValueError, "unrecognized"):
            kit["identify"]("00" * 32, {})

    def test_regular_bounded_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            regular = root / "regular"
            regular.write_bytes(b"abc")
            self.assertEqual(kit["measure"](regular, 3),
                             (3, bytes.fromhex("ba7816bf8f01cfea414140de5dae2223"
                                               "b00361a396177a9cb410ff61f20015ad")))
            with self.assertRaises(ValueError):
                kit["measure"](regular, 2)
            (root / "link").symlink_to(regular)
            os.mkfifo(root / "fifo")
            for name in ("link", "fifo"):
                with self.assertRaises(ValueError):
                    kit["measure"](root / name, 8)


if __name__ == "__main__":
    unittest.main()
