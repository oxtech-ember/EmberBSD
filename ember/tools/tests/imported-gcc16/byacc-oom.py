#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""AI-assisted fault injection into an actual generated backtracking parser."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--btyacc", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    generator = args.btyacc.resolve(strict=True)
    fixture = Path(__file__).resolve().parent
    cc = shlex.split(os.environ.get("CC", "gcc-16"))
    version = subprocess.check_output(cc + ["-dumpfullversion"], text=True).strip()
    if version.split(".")[0] != "16":
        parser.error("Use GCC 16")
    if args.work.exists():
        parser.error("Output directory already exists")
    work = args.work.resolve()
    work.mkdir(parents=True)
    results = {"compiler": version, "generator_sha256": hashlib.sha256(generator.read_bytes()).hexdigest(),
               "commands": [], "results": {}}

    def run(label, command):
        results["commands"].append(command)
        p = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=120, cwd=work)
        (work / (label + ".log")).write_text(p.stdout)
        results["results"][label] = p.returncode
        (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(label, p.returncode, flush=True)
        return p.returncode

    failures = 0
    for locations in (False, True):
        name = "locations" if locations else "plain"
        case_work = work / name
        case_work.mkdir()
        source = case_work / "parser-under-test.c"
        generate = [str(generator), "-B", "-d", "-l", "-o", str(source),
                    str(fixture / "parser-oom.y")]
        if locations:
            generate.insert(1, "-L")
        if run(name + "-generate", generate) != 0:
            failures += 1
            continue
        results[name + "_source_sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
        binary = case_work / "test"
        command = cc + ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
                        "-ftrivial-auto-var-init=pattern", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", "-I" + str(case_work)]
        command += shlex.split(os.environ.get("CFLAGS", ""))
        command += [str(fixture / "byacc-oom.c"), "-o", str(binary)]
        if run(name + "-build", command) != 0:
            failures += 1
            continue
        cases = ["parse", "zero", "0", "1", "2", "3"] + (["4"] if locations else [])
        for case in cases:
            failures += run(name + "-" + case, [str(binary), case]) != 0
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
