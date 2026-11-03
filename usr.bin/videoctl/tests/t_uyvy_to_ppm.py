#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Focused geometry and pixel checks for the host UYVY converter."""

import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

source = pathlib.Path(__file__).with_name("uyvy_to_ppm.py")
spec = importlib.util.spec_from_file_location("uyvy_to_ppm", source)
converter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(converter)


class UYVYToPPMTests(unittest.TestCase):
    def test_limited_601_red_pair_and_padded_stride(self):
        frame = bytes((90, 81, 240, 81, 0, 0, 0, 0))
        self.assertEqual(
            converter.convert(frame, 2, 1, 8, "601", "limited"),
            b"P6\n2 1\n255\n" + bytes((255, 0, 0, 255, 0, 0)),
        )

    def test_full_range_neutral_gray(self):
        self.assertEqual(
            converter.convert(bytes((128, 37, 128, 37)), 2, 1, 4,
                              "709", "full"),
            b"P6\n2 1\n255\n" + bytes((37, 37, 37, 37, 37, 37)),
        )

    def test_limited_below_black_keeps_chroma_before_rgb_clamp(self):
        self.assertEqual(
            converter.convert(bytes((128, 0, 255, 0)), 2, 1, 4,
                              "601", "limited"),
            b"P6\n2 1\n255\n" + bytes((184, 0, 0) * 2),
        )

    def test_rejects_invalid_geometry_and_short_frame(self):
        for args in (
            (bytes(4), 1, 1, 2, "601", "limited"),
            (bytes(4), 2, 1, 3, "601", "limited"),
            (bytes(3), 2, 1, 4, "601", "limited"),
        ):
            with self.subTest(args=args), self.assertRaises(ValueError):
                converter.convert(*args)

    def test_rejects_unaccounted_trailing_bytes(self):
        with self.assertRaises(ValueError):
            converter.convert(bytes(5), 2, 1, 4, "601", "limited")

    def test_cli_creates_private_ppm_without_overwriting(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            input_path = root / "frame.uyvy"
            output_path = root / "frame.ppm"
            input_path.write_bytes(bytes((90, 81, 240, 81)))
            command = [
                sys.executable, str(source), "--width", "2", "--height", "1",
                "--stride", "4", "--matrix", "601", "--range", "limited",
                str(input_path), str(output_path),
            ]
            subprocess.run(command, check=True)
            original = output_path.read_bytes()
            self.assertEqual(original, b"P6\n2 1\n255\n" + bytes((255, 0, 0) * 2))
            self.assertEqual(output_path.stat().st_mode & 0o777, 0o600)
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(output_path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
