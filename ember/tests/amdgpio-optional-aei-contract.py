#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Execute the AMD GPIO attach tail with optional event resources.

Set HOST_CC to an explicit host GCC 16 executable. This is an AI-assisted
control-flow model, not a firmware, interrupt, or GPIO hardware test.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
    source = (root / "sys/dev/acpi/amdgpio.c").read_text()
    function = source.index("\namdgpio_attach(device_t parent, device_t self, void *aux)\n{")
    start = source.index("rv = acpi_event_create_gpio(", function)
    end = source.index("\n}\n", start)
    tail = source[start:end]
    condition = "ACPI_FAILURE(rv) && rv != AE_NOT_FOUND"
    require(tail.count(condition) == 1, "missing unique optional-_AEI production guard")
    comments = re.findall(r"/\*.*?\*/", source[:source.index("#include")], re.DOTALL)
    require(any("Copyright" in item for item in comments), "missing source license")
    template = Path(__file__).with_name("amdgpio-optional-aei-fixture.c").read_text()
    fixture = template.replace("@SOURCE_COMMENTS@", "\n".join(comments))
    fixture = fixture.replace("@ATTACH_TAIL@", tail)
    require(not re.search(r"@[A-Z_]+@", fixture), "unexpanded fixture marker")
    cc = os.environ.get("HOST_CC", "")
    require(cc and Path(cc).is_absolute() and Path(cc).is_file(),
            "set HOST_CC to one explicit host GCC 16 executable")
    version = subprocess.check_output([cc, "-dumpfullversion"], text=True).strip()
    require(version.split(".")[0] == "16", "the contract requires host GCC 16")
    flags = ["-O1", "-g", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    flags += shlex.split(os.environ.get("HOST_CFLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="ember-amdgpio-aei-") as directory:
        work = Path(directory)

        def execute(name, text, expected_exit=0, diagnostic=None):
            src, binary = work / (name + ".c"), work / name
            src.write_text(text)
            build = subprocess.run([cc, *flags, str(src), "-o", str(binary)],
                                   capture_output=True, text=True, timeout=60)
            require(build.returncode == 0, "compile failed:\n" + build.stdout + build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            require(result.returncode == expected_exit,
                    name + ": unexpected exit:\n" + result.stdout + result.stderr)
            if diagnostic:
                require(diagnostic in result.stderr, name + ": missing causal diagnostic")
            else:
                require("AMD GPIO optional _AEI execution contract: PASS (9 cases)" in result.stdout,
                        "missing execution PASS marker")
            print(name + ": PASS")

        execute("production", fixture)
        execute("old-gate-control", fixture.replace(condition, "ACPI_FAILURE(rv)", 1), 1,
                "FAIL: absent _AEI must retain the GPIO provider")
        execute("ignored-errors-control", fixture.replace(condition, "((void)rv, 0)", 1), 1,
                "FAIL: event errors must abort before interrupt and GPIO registration")
    print("AMD GPIO optional _AEI contract: PASS (GCC " + version + "; 2 controls)")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
