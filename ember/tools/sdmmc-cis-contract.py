#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual SDMMC CMD52 path and checked CIS max-block query."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path, help="small source snapshot root")
p.add_argument("output", type=Path, help="new, empty output directory")
a = p.parse_args()
root = a.source.resolve()
out = a.output.resolve()
source = (root / "sys/dev/sdmmc/sdmmc_io.c").read_text()
header = (root / "sys/dev/sdmmc/sdmmcvar.h").read_text()


def function(name):
    match = re.search(
        r"(?m)^(?:static )?int\n" + re.escape(name) + r"\(", source)
    if match is None:
        return None
    end = source.index("\n}", match.end()) + 2
    return source[match.start():end] + "\n"


direct = function("sdmmc_io_rw_direct")
if direct is None:
    raise SystemExit("Missing production function: sdmmc_io_rw_direct")
query = function("sdmmc_io_maxblklen_checked")
if query is None:
    raise SystemExit("Missing production function: sdmmc_io_maxblklen_checked")
if not re.search(r"(?m)^int\s+sdmmc_io_maxblklen_checked\s*\(\s*"
                 r"struct sdmmc_function\s*\*\s*,\s*u_int\s*\*\s*\);",
                 header):
    raise SystemExit("Missing public declaration: sdmmc_io_maxblklen_checked")
if out.exists():
    raise SystemExit("output directory already exists: " + str(out))
out.mkdir(parents=True)

include = out / "include"
for relative in ("dev/sdmmc", "dev/pcmcia"):
    (include / relative).mkdir(parents=True, exist_ok=True)
for name in ("sdmmc_ioreg.h", "sdmmcreg.h", "sdmmc_io_status.h"):
    (include / "dev/sdmmc" / name).write_bytes(
        (root / "sys/dev/sdmmc" / name).read_bytes())
(include / "dev/pcmcia/pcmciareg.h").write_bytes(
    (root / "sys/dev/pcmcia/pcmciareg.h").read_bytes())

flags = ""
for match in re.finditer(r"(?m)^#define\s+SCF_.*(?:\\\n[^\n]*)*", header):
    flags += match.group(0) + "\n"
(out / "command-flags.inc").write_text(flags)
sf = re.search(r"(?ms)^struct sdmmc_function\s*\{.*?^\};", header)
number = None if sf is None else re.search(
    r"(?m)^[ \t]*int[ \t]+number[ \t]*;", sf.group(0))
if number is None:
    raise SystemExit("Missing production struct sdmmc_function.number")
(out / "function-number.inc").write_text(number.group(0).strip() + "\n")
(out / "production.inc").write_text(direct + query)

command = shlex.split(os.environ["HOST_CC"]) + [
    "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
    "-I" + str(include), "-I" + str(out),
] + shlex.split(os.environ.get("HOST_CFLAGS", ""))
command += [str(Path(__file__).with_suffix(".c")), "-o", str(out / "contract")]
subprocess.run(command, check=True)
subprocess.run([str(out / "contract")], check=True, timeout=60)
