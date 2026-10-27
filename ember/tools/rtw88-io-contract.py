#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual register helpers against an error-injecting bus."""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path, help="new output directory")
args = parser.parse_args()
root = args.source.resolve()
out = args.output.resolve()
header = (root / "sys/dev/ic/rtw88var.h").read_text(encoding="utf-8")


def declaration(name):
    match = re.search(r"^struct " + name + r" \{.*?^\};", header, re.M | re.S)
    if match is None:
        raise SystemExit("Missing production declaration: " + name)
    return match.group(0)


out.mkdir(parents=True, exist_ok=False)
include = out / "include/dev/ic"
include.mkdir(parents=True)
shim = header[:header.index("#ifndef")]
shim += "#ifndef RTW88_IO_CONTRACT_BUS_H\n#define RTW88_IO_CONTRACT_BUS_H\n"
shim += "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n"
shim += declaration("rtw88_dma") + "\n" + declaration("rtw88_bus_ops")
shim += "\n#endif\n"
(include / "rtw88var.h").write_text(shim, encoding="utf-8")
(include / "rtw88_io.h").write_bytes(
    (root / "sys/dev/ic/rtw88_io.h").read_bytes())
source = Path(__file__).with_suffix(".c")
command = shlex.split(os.environ["HOST_CC"]) + [
    "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fwrapv",
    "-I" + str(out / "include"),
] + shlex.split(os.environ.get("HOST_CFLAGS", ""))
subprocess.run(command + [str(source), "-o", str(out / "test")], check=True)
subprocess.run([str(out / "test")], check=True)
