#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the actual SDMMC CMD52 and checked interrupt registry code."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--direct-only", action="store_true")
a = p.parse_args()
root = a.source.resolve()
out = a.output.resolve()
source = (root / "sys/dev/sdmmc/sdmmc_io.c").read_text()
header = (root / "sys/dev/sdmmc/sdmmcvar.h").read_text()


def function(name):
    match = re.search(r"(?m)^(?:static )?(?:void\s*\*?|int)\n" + name + r"\(", source)
    if match is None:
        raise SystemExit("Missing production function: " + name)
    end = source.index("\n}", match.end()) + 2
    return source[match.start():end] + "\n"


body = function("sdmmc_io_rw_direct")
if not a.direct_only:
    names = ["sdmmc_intr_update_locked", "sdmmc_intr_set_checked",
             "sdmmc_intr_enable_checked", "sdmmc_intr_disable_checked",
             "sdmmc_intr_enable", "sdmmc_intr_disable",
             "sdmmc_intr_establish_checked", "sdmmc_intr_establish",
             "sdmmc_intr_disestablish_checked", "sdmmc_intr_disestablish"]
    declaration = re.search(r"(?ms)^struct sdmmc_intr_handler \{.*?^\};", source)
    if declaration is None:
        raise SystemExit("Missing production IRQ record")
    body = declaration.group(0) + "\n" + body
    for name in names:
        body += function(name)
    for name in [n for n in names if n.endswith("_checked") and n != "sdmmc_intr_set_checked"]:
        declaration = re.search(r"(?m)^int\s+" + name + r"\([^;]+;", header)
        if declaration is None:
            raise SystemExit("Missing public declaration: " + name)
        body = declaration.group(0) + "\n" + body
out.mkdir(parents=True, exist_ok=False)
include = out / "include/dev/sdmmc"
include.mkdir(parents=True)
for name in ("sdmmc_ioreg.h", "sdmmcreg.h", "sdmmc_io_status.h"):
    path = root / "sys/dev/sdmmc" / name
    if path.exists():
        (include / name).write_bytes(path.read_bytes())
flags = ""
for match in re.finditer(r"(?m)^#define\s+SCF_.*(?:\\\n[^\n]*)*", header):
    flags += match.group(0) + "\n"
(out / "command-flags.inc").write_text(flags)
sf = re.search(r"(?ms)^struct sdmmc_function\s*\{.*?^\};", header)
number = None if sf is None else re.search(
    r"(?m)^[ \t]*[A-Za-z_][A-Za-z_0-9 \t]*[ \t]+number[ \t]*;", sf.group(0))
if number is None:
    raise SystemExit("Missing production function-number declaration")
(out / "function-number.inc").write_text(number.group(0).lstrip() + "\n")
(out / "production.inc").write_text(body)
command = shlex.split(os.environ["HOST_CC"]) + [
    "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-pthread",
    "-I" + str(out), "-I" + str(out / "include"),
] + shlex.split(os.environ.get("HOST_CFLAGS", ""))
command.append("-UNDEBUG")
command += ["-DTEST_DIRECT_ONLY"] if a.direct_only else []
if (include / "sdmmc_io_status.h").exists():
    command.append("-DHAVE_SDMMC_IO_STATUS")
subprocess.run(command + [str(Path(__file__).with_suffix(".c")), "-o", str(out / "test")],
               check=True)
subprocess.run([str(out / "test")], check=True, timeout=30)
