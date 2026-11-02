#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project.
"""Run the actual descriptor builder/result code with host bus_dma types."""

import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
args = parser.parse_args()
source = pathlib.Path(__file__).with_suffix(".c")
with tempfile.TemporaryDirectory(prefix="esp-axi-gdma-test-") as name:
    work = pathlib.Path(name)
    (work / "sys").mkdir()
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
