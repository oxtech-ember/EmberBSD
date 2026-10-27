#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual x86 wakecode producer with explicitly selected host tools."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for role in ("cc", "ld", "nm", "make", "sh", "awk", "sed", "hexdump"):
    parser.add_argument("--" + role, required=True)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
owner = root / "sys/arch/x86/acpi"
args.output.mkdir(parents=True, exist_ok=False)
source = args.output / "source/sys"
acpi = source / "arch/x86/acpi"
acpi.mkdir(parents=True)
for name in ("genwakecode.sh", "wakecode.ldscript"):
    if (owner / name).exists():
        shutil.copyfile(owner / name, acpi / name)
shutil.copyfile(owner / "Makefile.wakecode.inc", args.output / "producer.mk")
assembly = source / "arch/amd64/acpi/acpi_wakecode.S"
assembly.parent.mkdir(parents=True)
assembly.write_text(""".text
.globl wakeup_16
.globl WAKEUP_marker
wakeup_16:
.byte 0x11, 0x22
WAKEUP_marker:
.byte 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
""")
(args.output / "Makefile").write_text('all: acpi_wakecode.h\n.include "producer.mk"\n')
command = [args.make, "-f", "Makefile", "all", "MACHINE=amd64",
           "S=" + str(source), "CC=" + args.cc, "LD=" + args.ld,
           "NM=" + args.nm, "HOST_SH=" + args.sh, "TOOL_AWK=" + args.awk,
           "TOOL_SED=" + args.sed, "TOOL_HEXDUMP=" + args.hexdump]
result = subprocess.run(command, cwd=args.output, capture_output=True, text=True)
(args.output / "build.log").write_text(result.stdout + result.stderr)
if result.returncode:
    raise RuntimeError(result.stdout + result.stderr)
header = (args.output / "acpi_wakecode.h").read_text()
match = re.search(r"^#define\s+WAKEUP_marker\s+(0x[0-9a-fA-F]+)$", header, re.M)
if not match or int(match[1], 16) != 2:
    raise RuntimeError("Missing or wrong relocated wakecode marker: " + header)
body = header.split("wakecode[] = {", 1)[1].split("};", 1)[0]
actual = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", body))
if actual != bytes.fromhex("1122334455667788"):
    raise RuntimeError("Wrong wakecode bytes: " + actual.hex())
failure = args.output / "fail-tool"
failure.write_text("#!" + args.sh + "\nexit 23\n")
failure.chmod(0o755)
for role in ("NM", "TOOL_HEXDUMP"):
    (args.output / "acpi_wakecode.h").unlink(missing_ok=True)
    broken = [item if not item.startswith(role + "=") else
              role + "=" + str(failure) for item in command]
    result = subprocess.run(broken, cwd=args.output, capture_output=True, text=True)
    (args.output / (role + "-failure.log")).write_text(result.stdout + result.stderr)
    if result.returncode == 0 or (args.output / "acpi_wakecode.h").exists():
        raise RuntimeError(role + " failure published a wakecode header")
print("x86 wakecode producer: PASS (exact payload and tool failure checks)")
