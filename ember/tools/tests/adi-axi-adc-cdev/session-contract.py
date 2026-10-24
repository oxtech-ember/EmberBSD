#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.

"""Check the receive-only ADC character-device contract."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[4]
HEADER = ROOT / "sys/dev/fdt/adi_axi_adc.h"
DRIVER = ROOT / "sys/dev/fdt/adi_axi_adc.c"
DMAC = ROOT / "sys/dev/fdt/adi_axi_dmac.c"
DMAC_API = ROOT / "sys/dev/fdt/adi_axi_dmacvar.h"
MAJORS = ROOT / "sys/conf/majors"
MAKEDEV = ROOT / "etc/MAKEDEV.tmpl"


class SessionContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.driver = DRIVER.read_text(encoding="utf-8")
        cls.dmac = DMAC.read_text(encoding="utf-8")
        cls.dmac_api = DMAC_API.read_text(encoding="utf-8")
        cls.majors = MAJORS.read_text(encoding="utf-8")
        cls.makedev = MAKEDEV.read_text(encoding="utf-8")

    def test_public_abi_is_rx_only_and_pointer_free(self):
        self.assertIn("ADI_AXI_ADCIOC_CONFIG", self.header)
        self.assertIn("ADI_AXI_ADCIOC_CAPTURE", self.header)
        self.assertIn("ADI_AXI_ADCIOC_STATUS", self.header)
        self.assertNotRegex(self.header, r"(?m)^#define\s+ADI_AXI_ADCIOC_.*TX")
        self.assertNotRegex(self.header, r"(?m)^\s*[^/\n]*\*\s*\w+\s*;")

    def test_one_rx1_iq_pair_and_finite_capture_limit_are_public(self):
        self.assertRegex(
            self.header,
            r"(?m)^#define\s+ADI_AXI_ADC_RX1_MASK\s+0x0*1U?\s*$",
        )
        self.assertRegex(
            self.header,
            r"(?m)^#define\s+ADI_AXI_ADC_RX1_IQ_CHANNEL_MASK\s+0x0*3U?\s*$",
        )
        self.assertRegex(
            self.header,
            r"(?m)^#define\s+ADI_AXI_ADC_CAPTURE_MAX_BYTES\s+\d+U?\s*$",
        )

    def test_device_has_no_write_or_mapping_entry_point(self):
        start = self.driver.find("const struct cdevsw adiadc_cdevsw")
        self.assertNotEqual(start, -1, "missing ADC character device")
        end = self.driver.find("};", start)
        self.assertNotEqual(end, -1, "unterminated cdevsw")
        cdevsw = self.driver[start:end]
        for member in (".d_open", ".d_close", ".d_read", ".d_ioctl"):
            self.assertIn(member, cdevsw)
        self.assertRegex(cdevsw, r"\.d_write\s*=\s*nowrite")
        self.assertRegex(cdevsw, r"\.d_mmap\s*=\s*nommap")

    def test_config_rejects_anything_but_rx1_iq(self):
        self.assertRegex(
            self.driver,
            r"rx_mask\s*!=\s*ADI_AXI_ADC_RX1_MASK",
        )
        self.assertRegex(
            self.driver,
            r"channel_mask\s*!=\s*ADI_AXI_ADC_RX1_IQ_CHANNEL_MASK",
        )

    def test_capture_guard_exposes_receive_only_fifo_to_memory_ops(self):
        for function in (
            "adi_axi_dmac_guard_bind",
            "adi_axi_dmac_guard_prepare",
            "adi_axi_dmac_guard_buffer_alloc",
            "adi_axi_dmac_guard_buffer_read",
            "adi_axi_dmac_guard_transfer",
            "adi_axi_dmac_guard_buffer_free",
        ):
            with self.subTest(function=function):
                self.assertIn(function, self.dmac_api)
                self.assertIn(function, self.dmac)
        start = self.dmac.find("adi_axi_dmac_guard_prepare(")
        self.assertNotEqual(start, -1, "missing guarded RX preparation")
        end = self.dmac.find("\n}", start)
        self.assertNotEqual(end, -1, "unterminated guarded RX preparation")
        prepare = self.dmac[start:end]
        self.assertIn("!sc->sc_read", prepare)
        self.assertIn("sc->sc_stream_source", prepare)
        self.assertIn("EOPNOTSUPP", prepare)

    def test_read_validates_length_before_dma_and_invalidates_old_data(self):
        start = self.driver.find("adiaxiadc_cdev_read(dev_t dev")
        self.assertNotEqual(start, -1, "missing bounded read implementation")
        end = self.driver.find("\n}", start)
        self.assertNotEqual(end, -1, "unterminated bounded read implementation")
        read = self.driver[start:end]
        validate = read.find("uio->uio_resid < capture_bytes")
        invalidate = read.find("sc_capture_valid = false")
        self.assertGreaterEqual(validate, 0, "undersized read must fail before DMA")
        self.assertGreaterEqual(invalidate, 0, "old capture must be invalidated")
        capture = read.find("adiaxiadc_capture_locked(sc)")
        self.assertGreater(capture, max(validate, invalidate))
        helper = self.driver.find("adiaxiadc_capture_locked(struct adi_axi_adc_softc *sc)")
        self.assertNotEqual(helper, -1, "missing finite capture operation")
        self.assertIn("adi_axi_dmac_guard_transfer(", self.driver[helper:])
        self.assertIn("capture_bytes % ADI_AXI_ADC_CAPTURE_ALIGNMENT", read)

    def test_capture_request_does_not_start_dma_before_read_size_is_known(self):
        start = self.driver.find("adiaxiadc_cdev_ioctl(dev_t dev")
        self.assertNotEqual(start, -1, "missing ioctl implementation")
        end = self.driver.find("\n}", start)
        self.assertNotEqual(end, -1, "unterminated ioctl implementation")
        ioctl = self.driver[start:end]
        self.assertNotIn("adi_axi_dmac_transfer(", ioctl)

    def test_static_device_node_is_created_root_only(self):
        self.assertRegex(
            self.majors,
            r"(?m)^device-major\s+adiadc\s+char\s+\d+\s+adiaxiadc\b",
        )
        self.assertIn("adiadc[0-9]*", self.makedev)
        self.assertRegex(
            self.makedev,
            r"mkdev\s+adiadc\$unit\s+c\s+%adiadc_chr%\s+\$unit\s+600",
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
