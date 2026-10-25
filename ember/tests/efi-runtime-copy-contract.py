#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the EFI foreign-memory copy boundary with explicit host GCC 16.

Run HOST_CC=/absolute/gcc16 python3 efi-runtime-copy-contract.py [SOURCE_ROOT].
The fixture models dispatch and ordering. It does not execute EFI or KASAN.
Provenance: NetBSD c8b00f7c39ae397b5be1bbe67e2500d042d3bdda, PR kern/60391.
The regression and fixture are AI-assisted EmberBSD additions.
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


def comments_before_include(source):
    boundary = source.find("#include")
    require(boundary >= 0, "missing source include boundary")
    comments = re.findall(r"/\*.*?\*/", source[:boundary], re.DOTALL)
    require(any("Copyright" in comment for comment in comments),
            "missing source copyright")
    return "\n".join(comments)


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
    source = (root / "sys/arch/x86/x86/efi_machdep.c").read_text()
    header = (root / "sys/lib/libkern/libkern.h").read_text()
    body = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    block = re.findall(
        r"pmap_update\(efi_runtime_pmap\);\s*"
        r"(?:\(memcpy\)|memcpy)\(&efi_rt, systbl->st_rt, sizeof\(efi_rt\)\);\s*"
        r"pmap_deactivate_sync\(efi_runtime_pmap, cookie\);", body)
    require(len(block) == 1, "missing unique production runtime copy block")
    require(body.count("(memcpy)") == 1 and "(memcpy)" in block[0],
            "raw memcpy must be confined to the EFI runtime read")
    start = header.index("/* Prototypes for which GCC built-ins exist. */")
    end = header.index("void\t*memmem(", start)
    declarations = header[start:end]
    wrapper = "#define\tmemcpy(d, s, l)\t\tkasan_memcpy(d, s, l)"
    require(declarations.count(wrapper) == 1, "missing unique production KASAN wrapper")
    template = Path(__file__).with_name("efi-runtime-copy-fixture.c").read_text()
    fixture = template.replace("@SOURCE_COMMENTS@",
                               comments_before_include(source) + "\n" + comments_before_include(header))
    fixture = fixture.replace("@MEMCPY_DECLARATIONS@", declarations)
    fixture = fixture.replace("@RUNTIME_COPY@", block[0])
    require(not re.search(r"@[A-Z_]+@", fixture), "unexpanded fixture marker")

    cc = os.environ.get("HOST_CC", "")
    require(cc and Path(cc).is_absolute() and Path(cc).is_file(),
            "set HOST_CC to one explicit host GCC 16 executable")
    version = subprocess.check_output([cc, "-dumpfullversion"], text=True).strip()
    require(version.split(".")[0] == "16", "the contract requires host GCC 16")
    flags = ["-O1", "-g", "-std=c11", "-Wall", "-Wextra", "-Werror",
             "-fno-builtin-memcpy"] + shlex.split(os.environ.get("HOST_CFLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="ember-efi-runtime-copy-") as directory:
        work = Path(directory)

        def execute(name, text, kasan, expected_exit=0, diagnostic=None):
            src, binary = work / (name + ".c"), work / name
            src.write_text(text)
            command = [cc, *flags, *(["-DKASAN"] if kasan else []), str(src), "-o", str(binary)]
            build = subprocess.run(command, capture_output=True, text=True, timeout=60)
            require(build.returncode == 0, "compile failed:\n" + build.stdout + build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            require(result.returncode == expected_exit,
                    name + ": unexpected exit:\n" + result.stdout + result.stderr)
            if diagnostic:
                require(diagnostic in result.stderr, name + ": missing causal diagnostic")
            else:
                require("EFI runtime copy execution contract: PASS" in result.stdout,
                        name + ": missing PASS marker")
            print(name + ": PASS")

        execute("ordinary", fixture, False)
        execute("kasan-dispatch", fixture, True)
        old_call = fixture.replace(block[0], block[0].replace("(memcpy)", "memcpy", 1), 1)
        execute("old-call-control", old_call, True, 1,
                "FAIL: foreign EFI read reached the KASAN wrapper")
        raw_macro = fixture.replace(wrapper, "#define memcpy(d, s, l) (memcpy)(d, s, l)", 1)
        execute("global-wrapper-control", raw_macro, True, 1,
                "FAIL: ordinary copy lost the KASAN wrapper")
    print("EFI runtime copy contract: PASS (GCC " + version + "; 4 cases)")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
