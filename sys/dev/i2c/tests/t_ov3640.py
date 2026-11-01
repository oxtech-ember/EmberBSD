#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run OV3640 identify and its actual FDT GPIO callbacks with native stubs."""

import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

source_dir = pathlib.Path(__file__).resolve().parent.parent
driver_source = (source_dir / "ov3640.c").read_text()
test_source = pathlib.Path(__file__).with_suffix(".c")
out_dir = pathlib.Path(args.out_dir).resolve()
out_dir.mkdir(parents=True, exist_ok=True)
binary = out_dir / "t_ov3640"
fixture = out_dir / "t_ov3640_driver.c"

def body(name):
    match = re.search(
        r"\n(?:static )?(?:int|void)\n" + name + r"\([^)]*\)\n\{",
        driver_source,
    )
    assert match, f"missing {name}"
    depth = 1
    end = match.end()
    while depth:
        if driver_source[end] == "{":
            depth += 1
        elif driver_source[end] == "}":
            depth -= 1
        end += 1
    return driver_source[match.start():end]


init_body = body("ov3640_init")
assert init_body.count("camera_sensor_power_release_flash(&sc->sc_power)") == 3


def extract(name):
    position = driver_source.index("\n" + name + "(")
    start = driver_source.rfind("\nstatic int", 0, position)
    end = driver_source.index("\n}", position) + 2
    if start < 0:
        raise ValueError(f"missing driver callback: {name}")
    return driver_source[start + 1 : end] + "\n"


fixture.write_text(
    r'''#include <stdbool.h>
struct fdtbus_gpio_pin;
struct camera_sensor_power {
    struct fdtbus_gpio_pin *reset, *powerdown;
};
struct ov3640_softc {
    struct camera_sensor_power sc_power;
    void *test_cookie;
    int test_reset_level, test_powerdown_level;
};
#define GPIO_PIN_LOW 0
#define GPIO_PIN_HIGH 1
void fdtbus_gpio_write(struct fdtbus_gpio_pin *, int);
'''
    + extract("ov3640_set_reset")
    + extract("ov3640_set_powerdown")
    + f'\n#include "{test_source}"\n'
)

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
        str(fixture),
        str(source_dir / "ov3640_logic.c"),
        "-o",
        str(binary),
    ],
    check=True,
)
subprocess.run([str(binary)], check=True)
print("OV3640 sensor sequence and ID contract passed")
