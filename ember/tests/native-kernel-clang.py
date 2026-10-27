#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check actual kernel macros and latency code with explicit native Clang tools."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", required=True)
parser.add_argument("--linker", required=True)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--sanitizer-traps", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
here = root / "ember/tests"
args.output.mkdir(parents=True, exist_ok=False)
drm = root / "sys/external/bsd/drm2/dist/drm"
rad = drm / "radeon"
acpi = root / "sys/external/bsd/acpica/dist/include"
source = (drm / "i915/intel_pm.c").read_text()
start = source.index("static bool ilk_increase_wm_latency(")
end = source.index("static void snb_wm_lp3_irq_quirk(", start)
(args.output / "i915-latency.inc").write_text(source[start:end])
cases = (("acpica", here / "acpica-address-fixture.c", [acpi]),
         ("atom-radeon", here / "atom-index-fixture.c", [rad]),
         ("atom-amd", here / "atom-index-fixture.c", [drm / "amd/include", rad]),
         ("i915", here / "i915-latency-fixture.c", [args.output]))
failed = []
for name, fixture, includes in cases:
    binary = args.output / name
    command = [args.cc, "--ld-path=" + args.linker, "-std=gnu11", "-O1", "-g",
               "-Wall", "-Wextra", "-Werror", "-Wnull-pointer-subtraction",
               "-Wbitwise-instead-of-logical"]
    if args.sanitizer_traps:
        command += ["-fsanitize=undefined,bounds", "-fsanitize-trap=all"]
    command += ["-I" + str(path) for path in includes]
    command += [str(fixture), "-o", str(binary)]
    result = subprocess.run(command, capture_output=True, text=True)
    (args.output / (name + "-build.log")).write_text(result.stdout + result.stderr)
    if result.returncode:
        failed.append(name)
        print(name + ": build FAIL", flush=True)
        continue
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
    (args.output / (name + "-run.log")).write_text(result.stdout + result.stderr)
    if result.returncode:
        failed.append(name)
    print(name + (": FAIL" if result.returncode else ": PASS"), flush=True)
if failed:
    raise SystemExit("Failed cases: " + ", ".join(failed))
