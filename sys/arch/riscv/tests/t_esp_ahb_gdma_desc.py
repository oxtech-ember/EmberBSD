#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise S31 AHB descriptors and the I2S shared-clock mode."""

import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
args = parser.parse_args()
source = pathlib.Path(__file__).with_suffix(".c")
i2s_source = source.parents[1] / "espressif" / "esp_i2s.c"
i2s_text = i2s_source.read_text(encoding="utf-8")
if "#define I2S_RX_SLAVE_MOD\t__BIT(3)" not in i2s_text:
    raise SystemExit("I2S RX slave mode bit is not defined as S31 RX_CONF bit 3")
if not __import__("re").search(
        r"I2S_WRITE\(sc, I2S_RX_CONF, I2S_RX_SLAVE_MOD\s*\|", i2s_text):
    raise SystemExit("I2S RX must consume the TX-generated BCLK and WS")
with tempfile.TemporaryDirectory(prefix="esp-ahb-gdma-test-") as name:
    work = pathlib.Path(name)
    (work / "sys").mkdir()
    (work / "sys" / "types.h").write_text(
        "#pragma once\n#include <stddef.h>\n#include <stdint.h>\n",
        encoding="utf-8")
    (work / "sys" / "bus.h").write_text(
        "#pragma once\n#include <stdint.h>\n"
        "typedef struct { uint64_t ds_addr; uint64_t ds_len; } "
        "bus_dma_segment_t;\n", encoding="utf-8")
    binary = work / "test"
    subprocess.run([args.cc, "-std=c11", "-O1", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-I", str(work),
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
