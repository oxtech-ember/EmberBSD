#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the real BMM150 fixed-point unit conversion with sanitizers."""

import argparse
import pathlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

source_dir = pathlib.Path(__file__).resolve().parent.parent
out_dir = pathlib.Path(args.out_dir).resolve()
out_dir.mkdir(parents=True, exist_ok=True)
binary = out_dir / "t_bmm150_math"
subprocess.run(
    [
        args.cc,
        "-std=c11",
        "-O1",
        "-g",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all",
        "-I",
        str(source_dir),
        str(pathlib.Path(__file__).with_suffix(".c")),
        str(source_dir / "bmm150_math.c"),
        "-o",
        str(binary),
    ],
    check=True,
)
subprocess.run([str(binary)], check=True)
print("BMM150 signed nanotesla conversion passed")
