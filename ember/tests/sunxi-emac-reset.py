#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the production EMAC reset and initialization with simulated MMIO."""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path, help="OS source root")
parser.add_argument("output", type=Path, help="new output directory")
args = parser.parse_args()
source = args.source / "sys/arch/arm/sunxi/sunxi_emac.c"
text = source.read_text(encoding="utf-8")


def function(name):
    match = re.search(r"^static int\n" + name + r"\([^\n]*\)\n\{.*?^\}",
                      text, re.M | re.S)
    if match is None:
        raise SystemExit("Missing production function: " + name)
    return match.group(0)


retry = re.search(r"^#define\s+SOFT_RST_RETRY\s+[^\n]+", text, re.M)
if retry is None:
    raise SystemExit("Missing reset retry limit")
fixture = Path(__file__).with_name("sunxi-emac-reset-fixture.c").read_text(encoding="utf-8")
code = fixture.replace("/* PRODUCTION LICENSE */", text[:text.index("#include")])
code = code.replace("/* PRODUCTION LIMIT */", retry.group(0))
code = code.replace("/* PRODUCTION FUNCTIONS */", "\n\n".join(
    function(name) for name in ("sunxi_emac_reset", "sunxi_emac_init_locked")))
args.output.mkdir(parents=True, exist_ok=False)
actual = args.output / "actual.c"
actual.write_text(code, encoding="utf-8")
command = shlex.split(os.environ["HOST_CC"]) + [
    "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
    "-I" + str(args.source / "sys/arch/arm/sunxi"),
] + shlex.split(os.environ.get("HOST_CFLAGS", ""))
subprocess.run(command + [str(actual), "-o", str(args.output / "test")], check=True)
subprocess.run([str(args.output / "test")], check=True)
