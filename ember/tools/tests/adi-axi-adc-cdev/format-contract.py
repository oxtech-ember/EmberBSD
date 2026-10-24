#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.

"""Check the public finite RX sample format and installed header."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[4]
HEADER = ROOT / "sys/dev/fdt/adi_axi_adc.h"


class FormatContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = HEADER.read_text(encoding="utf-8") if HEADER.is_file() else ""

    def test_public_header_exists(self):
        self.assertTrue(HEADER.is_file(), f"missing public header: {HEADER}")

    def test_complex_iq_sample_is_four_bytes(self):
        self.assertRegex(
            self.header,
            r"(?m)^#define\s+ADI_AXI_ADC_IQ_SAMPLE_BYTES\s+4U?\s*$",
        )

    def test_capture_alignment_is_eight_bytes(self):
        self.assertRegex(
            self.header,
            r"(?m)^#define\s+ADI_AXI_ADC_CAPTURE_ALIGNMENT\s+8U?\s*$",
        )

    def test_header_is_installed_under_dev_fdt(self):
        dev_makefile = (ROOT / "sys/dev/Makefile").read_text(encoding="utf-8")
        logical = re.sub(r"\\\n", " ", dev_makefile)
        subdirs = set()
        for match in re.finditer(r"(?m)^\s*SUBDIR\+?=\s*([^\n]*)", logical):
            subdirs.update(match.group(1).split())
        self.assertIn("fdt", subdirs)

        fdt_makefile = ROOT / "sys/dev/fdt/Makefile"
        self.assertTrue(fdt_makefile.is_file(), "missing FDT include Makefile")
        text = fdt_makefile.read_text(encoding="utf-8")
        self.assertRegex(text, r"(?m)^INCSDIR\s*=\s*/usr/include/dev/fdt\s*$")
        self.assertRegex(text, r"(?m)^INCS\s*\+?=\s*adi_axi_adc\.h\s*$")

        comp_sets = (ROOT / "distrib/sets/lists/comp/mi").read_text(
            encoding="utf-8"
        )
        self.assertRegex(
            comp_sets,
            r"(?m)^\./usr/include/dev/fdt/adi_axi_adc\.h\s+comp-c-include\s*$",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
