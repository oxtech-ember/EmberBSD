#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile actual clock and MAC functions against controlled register/MII fixtures."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = Path(os.environ.get("SOURCE_ROOT", here.parents[3]))
clock = (root / "sys/arch/arm/xilinx/zynq7000_clkc.c").read_text()
mac = (root / "sys/dev/cadence/if_cemac.c").read_text()

def function(source, name):
    match = re.search(r"static (?:u_int|int|void)\n" + name + r"\([^;]+?\n\{", source)
    if not match:
        raise SystemExit("missing actual implementation: " + name)
    end = source.index("\n}", match.end()) + 2
    return source[match.start():end]

functions = "\n".join(function(clock, name) for name in (
    "zynq7000_clkc_gem_parent", "zynq7000_clkc_get_rate_gem",
    "zynq7000_clkc_clk_set_rate"))
functions += "\n" + function(mac, "cemac_statchg")
defines = "\n".join(line for line in clock.splitlines() if line.startswith("#define"))
with tempfile.TemporaryDirectory(prefix="zynq-gem-clock-") as directory:
    source = Path(directory) / "contract.c"
    binary = Path(directory) / "contract"
    source.write_text((here / "fixture.c").read_text().replace(
        "/* ACTUAL_DRIVER */", defines + "\n" + functions))
    command = shlex.split(os.environ.get("CC", "cc"))
    command += ["-std=c11", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-O2", "-g"]
    command += shlex.split(os.environ.get("CFLAGS", ""))
    if os.environ.get("ANALYZE") == "yes":
        command += ["-fanalyzer"]
        subprocess.run(["cppcheck", "--enable=warning,performance,portability",
                        "--error-exitcode=1", "--suppress=missingIncludeSystem", str(source)], check=True)
    subprocess.run(command + [str(source), "-o", str(binary)], check=True)
    runner = [str(binary)]
    if os.environ.get("MEMCHECK") == "yes":
        runner = ["valgrind", "--error-exitcode=1", "--leak-check=full"] + runner
    subprocess.run(runner, check=True)
