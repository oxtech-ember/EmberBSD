#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the production RTL8723D bootstrap against a bounded register model."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
root = a.source.resolve()
out = a.output.resolve()
source = root / "sys/dev/ic/rtw8723d_boot.c"
boot_header = root / "sys/dev/ic/rtw8723d_boot.h"
if not source.exists() or not boot_header.exists():
    raise SystemExit("Missing RTL8723D bootstrap implementation")
out.mkdir(parents=True, exist_ok=False)
include = out / "include/dev/ic"
include.mkdir(parents=True)
header = (root / "sys/dev/ic/rtw88var.h").read_text(encoding="utf-8")
shim = header[:header.index("#ifndef")]
shim += "#ifndef RTW8723D_CONTRACT_BUS_H\n#define RTW8723D_CONTRACT_BUS_H\n"
shim += "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n"
for name in ("rtw88_dma", "rtw88_bus_ops"):
    match = re.search(r"^struct " + name + r" \{.*?^\};", header, re.M | re.S)
    if match is None:
        raise SystemExit("Missing production declaration: " + name)
    shim += match.group(0) + "\n"
shim += "#endif\n"
(include / "rtw88var.h").write_text(shim, encoding="utf-8")
for name in ("rtw88_io.h", "rtw8723d_boot.h"):
    (include / name).write_bytes((root / "sys/dev/ic" / name).read_bytes())
command = shlex.split(os.environ["HOST_CC"]) + [
    "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fwrapv",
    "-I" + str(out / "include"),
] + shlex.split(os.environ.get("HOST_CFLAGS", ""))
subprocess.run(command + [str(source), str(Path(__file__).with_suffix(".c")),
                          "-o", str(out / "test")], check=True)
subprocess.run([str(out / "test")], check=True)
