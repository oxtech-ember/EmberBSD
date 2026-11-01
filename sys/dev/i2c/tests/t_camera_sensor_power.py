#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual common camera sensor resource acquisition helper natively."""

import argparse
import pathlib
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

position = driver_source.index("\ncamera_sensor_power_acquire(")
start = driver_source.rfind("\nint", 0, position)
end = driver_source.index("\n}", position) + 2
if start < 0:
    raise ValueError("missing actual resource acquisition helper")
fixture.write_text(f'#include "{test_source}"\n' + driver_source[start + 1 : end] + "\n")

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
