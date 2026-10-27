#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Build and run the diagnostic lifecycle fixture with explicit native tools."""
import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", required=True, type=Path)
parser.add_argument("--linker", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[3]
args.output.mkdir(parents=True, exist_ok=False)
include = args.output / "include/dev/usb"
include.mkdir(parents=True)
shutil.copyfile(root / "sys/dev/usb/usbperiph.h", include / "usbperiph.h")
binary = args.output / "function-test"
subprocess.run([str(args.cc), "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-O1", "-g", "--ld-path=" + str(args.linker),
                "-I" + str(args.output / "include"),
                str(here / "function-test.c"), "-o", str(binary)], check=True)
for case in ("reconfigure", "stable", "reset", "initial-suspend",
             "initial-disconnect", "stale-disconnect",
             "suspend", "disconnect", "late-cancel"):
    result = subprocess.run([str(binary), case], capture_output=True, text=True,
                            timeout=5)
    expected = 1 if case in ("initial-disconnect", "suspend", "disconnect",
                             "late-cancel") else 0
    if result.returncode != expected:
        raise RuntimeError(f"{case}: expected {expected}, got {result.returncode}\n"
                           + result.stdout + result.stderr)
    if expected and "completed " in result.stdout:
        raise RuntimeError(f"{case}: failed transfer reported completion")
    print(f"{case}: PASS")
print("diagnostic configuration lifecycle: PASS")
