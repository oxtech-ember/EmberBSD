#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the production DVP lifecycle state transitions as a host fixture."""

import argparse
import pathlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

test = pathlib.Path(__file__).with_suffix(".c")
binary = pathlib.Path(args.out_dir).resolve() / "t_esp32s31_dvp_lifecycle"
binary.parent.mkdir(parents=True, exist_ok=True)
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
        str(test),
        "-o",
        str(binary),
    ],
    check=True,
)
subprocess.run([str(binary)], check=True)
print("ESP32-S31 DVP lifecycle passed")
