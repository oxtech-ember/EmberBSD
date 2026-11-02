#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the real video sensor callback contract as a host fixture."""

import argparse
import pathlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

test = pathlib.Path(__file__).with_suffix(".c")
binary = pathlib.Path(args.out_dir).resolve() / "t_video_sensor_if"
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
print("video sensor callback interface passed")
