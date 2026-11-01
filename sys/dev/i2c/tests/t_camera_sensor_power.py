#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual common camera sensor resource acquisition helper natively."""

import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

source_dir = pathlib.Path(__file__).resolve().parent.parent
driver_source = (source_dir / "camera_sensor_power.c").read_text()
test_source = pathlib.Path(__file__).with_suffix(".c")
out_dir = pathlib.Path(args.out_dir).resolve()
out_dir.mkdir(parents=True, exist_ok=True)
binary = out_dir / "t_camera_sensor_power"
fixture = out_dir / "t_camera_sensor_power_driver.c"


def extract(name):
    match = re.search(r"\n(?:int|void)\n" + name + r"\([^)]*\)\n\{", driver_source)
    if match is None:
        raise ValueError(f"missing actual helper: {name}")
    depth = 1
    end = match.end()
    while depth:
        if driver_source[end] == "{":
            depth += 1
        elif driver_source[end] == "}":
            depth -= 1
        end += 1
    return driver_source[match.start() + 1 : end] + "\n"


fixture.write_text(
    f'#include "{test_source}"\n'
    + extract("camera_sensor_power_acquire")
    + extract("camera_sensor_power_release_flash")
)

subprocess.run(
    [
        args.cc,
        "-std=c11",
        "-D__KERNEL_RCSID(a,b)=",
        "-O1",
        "-g",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all",
        "-I",
        str(source_dir),
        str(fixture),
        "-o",
        str(binary),
    ],
    check=True,
)
subprocess.run([str(binary)], check=True)
print("camera sensor shared power and safe failure contract passed")
