#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise FDT I2C enumeration with real status and OF enumeration functions."""

import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

repo = pathlib.Path(__file__).resolve().parents[4]
fdt_dir = repo / "sys/dev/fdt"
of_source = (repo / "sys/dev/ofw/ofw_i2c_subr.c").read_text()
fdt_source = (fdt_dir / "fdt_i2c.c").read_text()
status_source = (fdt_dir / "fdt_subr.c").read_text()
test_source = pathlib.Path(__file__).with_suffix(".c")
out_dir = pathlib.Path(args.out_dir).resolve()
out_dir.mkdir(parents=True, exist_ok=True)
fixture = out_dir / "t_fdt_i2c_status.c"
binary = out_dir / "t_fdt_i2c_status"


def extract(source, name, return_type):
    pattern = re.compile(
        rf"\n{re.escape(return_type)}\s+{re.escape(name)}\([^;]*?\n\{{.*?\n\}}",
        re.S,
    )
    match = pattern.search(source)
    if match is None:
        raise ValueError(f"missing actual function {name}")
    return match.group(0).strip() + "\n"


actual = "\n".join(
    (
        # Suppress only the inherited int-vs-size_t comparisons in this helper.
        "#pragma clang diagnostic push\n"
        '#pragma clang diagnostic ignored "-Wsign-compare"\n'
        + extract(of_source, "of_enter_i2c_devs_filtered", "void")
        + extract(of_source, "of_enter_i2c_devs", "void")
        + "#pragma clang diagnostic pop\n",
        extract(status_source, "fdtbus_status_okay", "bool"),
        extract(fdt_source, "fdtbus_attach_i2cbus", "device_t"),
    )
)
fixture.write_text(test_source.read_text() + "\n" + actual)

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
        str(fixture),
        "-o",
        str(binary),
    ],
    check=True,
)
subprocess.run([str(binary)], check=True)
print("FDT I2C status filtering and legacy OF enumeration passed")
