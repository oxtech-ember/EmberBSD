#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted tipout signal regression.
"""Compile usr.bin/tip/tipout.c and require cleanup outside the signal handler.

Example: HOST_GCC16_FLAGS='-isysroot /path/to/MacOSX.sdk' \
  python3 ember/tests/tip-stop.py --cc /path/to/gcc-16 \
  --baseline HEAD /absolute/new-work-directory
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path("usr.bin/tip/tipout.c")
STUB = r"""
#include <sys/cdefs.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define TRUE 1
#define FALSE 0
typedef struct { char *v_value; } value_t;
extern value_t vtable[];
#define value(v) vtable[v].v_value
#define boolean(v) ((short)(intptr_t)(v))
#define setboolean(v, n) do { (v) = (char *)(intptr_t)(n); } while (0)
#define BEAUTIFY 0
#define EXCEPTIONS 7
#define SCRIPT 17
extern FILE *fscript;
extern int attndes[2], fildes[2], repdes[2], FD, bits8;
extern char ccc;
#define STRIP_PAR 0177
int any(char, const char *);
"""


def source_text(revision):
    if revision is None:
        return (ROOT / SOURCE).read_text()
    return subprocess.check_output(
        ["git", "show", revision + ":" + str(SOURCE)], cwd=ROOT, text=True)


def run_one(cc, flags, work, name, text, sanitize):
    case = work / name
    case.mkdir()
    (case / "tip.h").write_text(STUB)
    (case / "tipout.c").write_text(text)
    exe = case / ("tip-stop-san" if sanitize else "tip-stop")
    command = [cc, *flags, "-I", str(case), str(case / "tipout.c"),
               str(ROOT / "ember/tests/tip-stop.c"), "-o", str(exe)]
    if sanitize:
        command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    build = subprocess.run(command, text=True, capture_output=True)
    (case / "build.log").write_text(build.stdout + build.stderr)
    result = {
        "sha256": hashlib.sha256(text.encode()).hexdigest(),
        "compile_exit": build.returncode,
        "command": command,
    }
    if build.returncode != 0:
        result["pass"] = False
        result["error"] = "compile failed"
        return result
    proc = subprocess.run([str(exe)], text=True, capture_output=True, timeout=20)
    (case / "run.log").write_text(proc.stdout + proc.stderr)
    result["run_exit"] = proc.returncode
    result["stdout"] = proc.stdout
    result["stderr"] = proc.stderr
    result["pass"] = proc.returncode == 0
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("HOST_GCC16", "gcc-16"))
    parser.add_argument("--baseline", help="Git revision that must still fail this contract")
    parser.add_argument("work", type=Path)
    args = parser.parse_args()
    work = args.work.expanduser().absolute()
    if not args.work.is_absolute() or work.exists() or not work.parent.is_dir():
        parser.error("work must be a new absolute directory with an existing parent")
    version = subprocess.check_output([args.cc, "-dumpfullversion"], text=True).strip()
    if not version.startswith("16."):
        parser.error("a real GCC16 host compiler is required")
    flags = shlex.split(os.environ.get("HOST_GCC16_FLAGS", ""))
    flags += ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
              "-Wno-deprecated-declarations"]
    work.mkdir()
    results = {"compiler": args.cc, "version": version, "flags": flags, "cases": {}}
    failed = False
    current = source_text(None)
    for sanitize in (False, True):
        label = "candidate-asan" if sanitize else "candidate"
        result = run_one(args.cc, flags, work, label, current, sanitize)
        results["cases"][label] = result
        print(label, "PASS" if result["pass"] else "FAIL", flush=True)
        if result.get("stderr"):
            print(result["stderr"], file=sys.stderr, end="")
        if not result["pass"]:
            failed = True
    if args.baseline:
        previous = source_text(args.baseline)
        result = run_one(args.cc, flags, work, "baseline", previous, True)
        result["pass_expected"] = False
        result["reproduced"] = not result["pass"]
        results["cases"]["baseline"] = result
        print("baseline", "REPRODUCED" if result["reproduced"] else "MISSED", flush=True)
        if result.get("stderr"):
            print(result["stderr"], file=sys.stderr, end="")
        if result["compile_exit"] != 0 or result["pass"]:
            failed = True
            result["error"] = "baseline must compile and fail the contract"
    (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
