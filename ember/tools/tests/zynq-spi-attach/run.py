#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise actual Zynq SPI attach code with failing clock/FDT/MMIO providers."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"static (?:uint32_t|int|void)\n" + name + r"\([^;]+?\n\{", source)
    if not match:
        raise SystemExit("missing actual implementation: " + name)
    end = source.index("\n}", match.end()) + 2
    return source[match.start():end]


here = Path(__file__).resolve().parent
root = Path(os.environ.get("SOURCE_ROOT", here.parents[3]))
driver = (root / "sys/arch/arm/xilinx/zynq_spi.c").read_text(encoding="utf-8")
registers = (root / "sys/arch/arm/xilinx/zynq_spireg.h").read_text(encoding="utf-8")
softc = re.search(r"struct zynq_spi_softc \{.*?\n};", driver, re.DOTALL)
if softc is None:
    raise SystemExit("missing actual zynq_spi_softc")
functions = "\n".join(function(driver, name) for name in (
    "zynq_spi_read", "zynq_spi_write", "zynq_spi_drain", "zynq_spi_attach"))
fixture = (here / "fixture.c").read_text(encoding="utf-8")
fixture = fixture.replace("/* ACTUAL_REGISTERS */", registers)
fixture = fixture.replace("/* ACTUAL_SOFTC */", softc.group())
fixture = fixture.replace("/* ACTUAL_DRIVER */", functions)
with tempfile.TemporaryDirectory(prefix="zynq-spi-attach-") as directory:
    source = Path(directory) / "contract.c"
    binary = Path(directory) / "contract"
    source.write_text(fixture, encoding="utf-8")
    command = shlex.split(os.environ.get("CC", "gcc-16"))
    command += ["-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-O2", "-g"]
    command += shlex.split(os.environ.get("CFLAGS", ""))
    subprocess.run(command + [str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
