#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Execute the actual Zynq clock provider with a protected SLCR fixture."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = Path(os.environ.get("SOURCE_ROOT", here.parents[3]))
source = (root / "sys/arch/arm/xilinx/zynq7000_clkc.c").read_text()

def function(name):
    match = re.search(r"static (?:u_int|int)\n" + name + r"\([^;]+?\n\{", source)
    if match is None:
        raise SystemExit("missing actual function " + name)
    return source[match.start():source.index("\n}", match.end()) + 2]

body = "\n".join(line for line in source.splitlines() if line.startswith("#define"))
body += "\n" + re.search(r"enum xynq7000_clkid \{.*?\n};", source, re.S).group()
body += "\n" + re.search(r"struct zynq7000_clkc_softc \{.*?\n};", source, re.S).group()
body += "\nstatic u_int zynq7000_clkc_clk_get_rate(void *, struct clk *);\n"
body += "\n".join(function(name) for name in (
    "zynq7000_clkc_get_rate_pll", "zynq7000_clkc_get_rate_iop",
    "zynq7000_clkc_gem_parent", "zynq7000_clkc_get_rate_gem",
    "zynq7000_clkc_clk_get_rate", "zynq7000_clkc_clk_enable"))
# The extracted view omits attachment users of these complete softc fields.
for field in ("\tdevice_t\t\tsc_dev;", "\tstruct clk_domain\tsc_clkdom;"):
    body = body.replace(field, "// cppcheck-suppress unusedStructMember\n" + field)
# These existing callback signatures follow clk_funcs; const changes its ABI.
for signature in ("static u_int\nzynq7000_clkc_clk_get_rate(",
                  "static int\nzynq7000_clkc_clk_enable("):
    body = body.replace(signature, signature.replace("\n", "\n// cppcheck-suppress constParameterPointer\n", 1))
# This inherited declaration serves disjoint branches of the complete getter.
body = body.replace("\tu_int prate, div0, div1, sel, unit;",
                    "// cppcheck-suppress variableScope\n\tu_int prate, div0, div1, sel, unit;")
with tempfile.TemporaryDirectory(prefix="zynq-usb-clock-") as directory:
    work = Path(directory)
    contract = work / "contract.c"
    contract.write_text((here / "fixture.c").read_text().replace("/* ACTUAL_DRIVER */", body))
    command = shlex.split(os.environ["CC"])
    command += ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]
    command += shlex.split(os.environ.get("CFLAGS", ""))
    if os.environ.get("ANALYZE") == "yes":
        subprocess.run([os.environ["CPPCHECK"], "--enable=warning,style,performance,portability",
                        "--error-exitcode=1", "--inline-suppr", "--suppress=missingIncludeSystem", str(contract)], check=True)
        command += ["-fanalyzer"]
    binary = work / "contract"
    subprocess.run(command + [str(contract), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
