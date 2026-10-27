#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile libc compatibility definitions and execute their native regex/free path.

Select --cc, --sysroot, --target and host --cflags-json (an argv array).
The target check compiles the actual library sources with prototype diagnostics
as errors. The native check uses the actual regexp headers/implementations and
cfree; only platform symbol naming and diagnostic assertions are adapted.
--sanitize adds ASan/UBSan. It does not execute target tty or network operations.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

SOURCES = ("4.1/ftime.c", "4.1/gtty.c", "4.1/stty.c", "4.3/cfree.c",
           "4.3/regex.c", "4.3/rexec.c", "4.3/ruserpass.c", "4.4/cuserid.c",
           "regexp/regexp.c", "regexp/regsub.c")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True, type=Path)
    parser.add_argument("--sysroot", required=True, type=Path)
    parser.add_argument("--target", required=True)
    parser.add_argument("--cflags-json", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    if not args.cc.is_absolute() or not args.cc.is_file():
        parser.error("--cc must select an existing absolute compiler")
    if not args.sysroot.is_absolute() or not args.sysroot.is_dir():
        parser.error("--sysroot must select existing absolute target headers")
    flags = json.loads(args.cflags_json.read_text())
    if not isinstance(flags, list) or not all(isinstance(v, str) for v in flags):
        parser.error("--cflags-json must be a string array")
    args.work.mkdir(parents=True, exist_ok=False)
    work = args.work.resolve()
    root = Path(__file__).resolve().parents[2]
    library = root / "lib/libcompat"
    commands = []

    def run(argv):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=90)
        commands.append(dict(argv=argv, status=result.returncode,
                             output=result.stdout + result.stderr))
        (work / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    strict = ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
              "-Wstrict-prototypes", "-Wmissing-prototypes",
              "-Wdeprecated-non-prototype", "-D__LIBCOMPAT_SOURCE__"]
    for source in SOURCES:
        run([str(args.cc), "--no-default-config", "--target=" + args.target,
             "--sysroot=" + str(args.sysroot), *strict, "-c", str(library / source),
             "-o", str(work / (Path(source).stem + ".o"))])
    run([str(args.cc), "--no-default-config", "--target=" + args.target,
         "--sysroot=" + str(args.sysroot), *strict, "-DDEBUG", "-c",
         str(library / "regexp/regexp.c"), "-o", str(work / "regexp-debug.o")])
    print("target compatibility sources, including regexp DEBUG: PASS", flush=True)
    for header in ("regexp.h", "re_comp.h"):
        shutil.copyfile(root / "include" / header, work / header)
    (work / "host.h").write_text("""#include <sys/cdefs.h>
#include <assert.h>
#include <stdlib.h>
#ifndef _DIAGASSERT
#define _DIAGASSERT(e) assert(e)
#endif
#ifndef __RENAME
#ifdef __APPLE__
#define __RENAME(x) __asm__("_" #x)
#else
#define __RENAME(x) __asm__(#x)
#endif
#endif
void cfree(void *);
""")
    native = [str(args.cc), *flags, *strict, "-I" + str(work),
              "-include", str(work / "host.h")]
    sources = [str(library / name) for name in
               ("4.3/cfree.c", "4.3/regex.c", "regexp/regexp.c", "regexp/regsub.c")]
    fixture = root / "ember/tests/libcompat-prototypes-fixture.c"
    modes = [("native", [])]
    if args.sanitize:
        modes.append(("sanitized", ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]))
    for name, extra in modes:
        binary = work / name
        run(native + extra + sources + [str(fixture), "-o", str(binary)])
        run([str(binary)])
        print(name + " regex/cfree: PASS", flush=True)


if __name__ == "__main__":
    main()
