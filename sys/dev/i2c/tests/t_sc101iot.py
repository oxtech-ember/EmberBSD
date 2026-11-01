#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run SC101IOT ID logic and the real driver I2C/GPIO callbacks natively."""

import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

source_dir = pathlib.Path(__file__).resolve().parent.parent
driver_source = (source_dir / "sc101iot.c").read_text()
power_source = (source_dir / "camera_sensor_power.c").read_text()
test_source = pathlib.Path(__file__).with_suffix(".c")
out_dir = pathlib.Path(args.out_dir).resolve()
out_dir.mkdir(parents=True, exist_ok=True)
binary = out_dir / "t_sc101iot"
fixture = out_dir / "t_sc101iot_driver.c"


def extract(name):
    position = driver_source.index("\n" + name + "(")
    start = driver_source.rfind("\nstatic ", 0, position)
    end = driver_source.index("\n}", position) + 2
    if start < 0:
        raise ValueError(f"missing actual driver function: {name}")
    return driver_source[start + 1 : end] + "\n"


def extract_power(name):
    match = re.search(
        r"\n(?:int|void)\n" + name + r"\([^)]*\)\n\{", power_source
    )
    if match is None:
        raise ValueError(f"missing actual power helper: {name}")
    depth = 1
    end = match.end()
    while depth:
        if power_source[end] == "{":
            depth += 1
        elif power_source[end] == "}":
            depth -= 1
        end += 1
    return power_source[match.start() + 1 : end] + "\n"


fixture.write_text(
    f'#include "{test_source}"\n'
    + extract("sc101iot_set_reset")
    + extract("sc101iot_set_powerdown")
    + extract("sc101iot_delay_us")
    + extract("sc101iot_read_reg")
    + extract("sc101iot_read_reg_cb")
    + extract("sc101iot_init")
    + extract_power("camera_sensor_power_release_flash")
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
        str(source_dir / "sc101iot_logic.c"),
        "-o",
        str(binary),
    ],
    check=True,
)
subprocess.run([str(binary)], check=True)
print("SC101IOT page protocol, power sequence and PID contract passed")
