#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project.
"""Exercise production GDMA transfer, interrupt and result against MMIO."""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
args = parser.parse_args()
here = pathlib.Path(__file__).resolve().parent
srcdir = here.parent / "espressif"
source = (srcdir / "esp_axi_gdma.c").read_text(encoding="utf-8")
prefix = source[source.index("#define GDMA_CHANNELS"):source.index(
    "static int esp_axi_gdma_intr(void *);")]
parts = [prefix]
for name, result in [("softintr", "void"), ("acquire", "void *"),
                     ("transfer", "int"), ("intr", "int"),
                     ("parked", "bool"), ("result", "int"),
                     ("halt_checked", "int"), ("halt", "void")]:
    start = source.index("static " + result + "\nesp_axi_gdma_" + name + "(")
    end = source.index("\n}\n", start) + 3
    parts.append(source[start:end])
fixture = (here / "t_esp_axi_gdma_mmio.c").read_text(encoding="utf-8")
fixture = fixture.replace("/* INCLUDE_PRODUCTION */", "\n".join(parts))
with tempfile.TemporaryDirectory(prefix="esp-axi-gdma-mmio-") as name:
    work = pathlib.Path(name)
    (work / "sys").mkdir()
    (work / "sys" / "bus.h").write_text(
        "#pragma once\n#include <stdint.h>\n"
        "typedef struct { uint64_t ds_addr; uint64_t ds_len; } "
        "bus_dma_segment_t;\n", encoding="utf-8")
    (work / "test.c").write_text(fixture, encoding="utf-8")
    subprocess.run([args.cc, "-std=c11", "-O1", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-I", str(work),
                    "-I", str(srcdir), str(work / "test.c"),
                    str(srcdir / "esp_axi_gdma_desc.c"), "-o",
                    str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
