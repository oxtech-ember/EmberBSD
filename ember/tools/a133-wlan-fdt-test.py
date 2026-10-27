#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the native YS-M33 DTB with the production A133 FDT adapter."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
root = a.source.resolve()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
cc = shlex.split(os.environ["HOST_CC"])
dtc = shlex.split(os.environ["HOST_DTC"])
dtsroot = root / "sys/external/gpl2/dts/dist"
boarddir = dtsroot / "arch/arm64/boot/dts/allwinner"
board = boarddir / "sun50i-a133-ys-m33.dts"
with (out / "board.dts").open("wb") as stream:
    subprocess.run(cc + ["-E", "-P", "-x", "assembler-with-cpp",
                         "-nostdinc", "-undef", "-D__DTS__",
                         "-I" + str(boarddir), "-I" + str(dtsroot / "include"),
                         str(board)], stdout=stream, check=True)
subprocess.run(dtc + ["-I", "dts", "-O", "dtb", "-o", str(out / "board.dtb"),
                      str(out / "board.dts")], check=True)
flags = ["-std=c99", "-O1", "-g", "-Wall", "-Wextra", "-Werror"]
flags += shlex.split(os.environ.get("HOST_CFLAGS", ""))
ldflags = shlex.split(os.environ.get("HOST_LDFLAGS", ""))
subprocess.run(cc + flags + [str(Path(__file__).with_suffix(".c")),
                            str(root / "sys/arch/arm/sunxi/sun50i_a133_fdt.c")]
               + ldflags + ["-lfdt", "-o", str(out / "test")], check=True)
subprocess.run([str(out / "test"), str(out / "board.dtb")], check=True)
