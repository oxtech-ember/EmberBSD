#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Check actual byacc generators and a missing-counter-use negative control."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--byacc", required=True, type=Path)
    parser.add_argument("--btyacc", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    cc = shlex.split(os.environ.get("CC", "gcc-16"))
    version = subprocess.check_output(cc + ["-dumpfullversion"], text=True).strip()
    if version.split(".")[0] != "16":
        parser.error("Use GCC 16")
    if args.work.exists():
        parser.error("Output directory already exists")
    work = args.work.resolve()
    work.mkdir(parents=True)
    grammar = Path(__file__).with_name("parser.y").read_text()
    results = {"compiler": version, "generators": {}, "commands": [], "results": {}}

    def require(condition, detail):
        if not condition:
            raise RuntimeError(detail)

    def run(label, cmd):
        results["commands"].append(cmd)
        p = subprocess.run(cmd, text=True, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=120)
        (work / (label + ".log")).write_text(p.stdout)
        results["results"][label] = p.returncode
        (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        return p

    for name, generator in (("byacc", args.byacc), ("btyacc", args.btyacc)):
        generator = generator.resolve(strict=True)
        results["generators"][name] = hashlib.sha256(generator.read_bytes()).hexdigest()
        for reads_counter in (False, True):
            label = name + ("-counter" if reads_counter else "-optional")
            text = grammar.replace("{ yyerrok; }", "{ assert(yynerrs == 1); yyerrok; }") if reads_counter else grammar
            g = work / (label + ".y")
            g.write_text(text)
            source = work / (label + ".c")
            command = [str(generator), "-l", "-o", str(source), str(g)]
            if name == "btyacc":
                command.insert(1, "-B")
            require(run(label + "-generate", command).returncode == 0, "Parser generation failed")
            for negative in (False, True):
                case = label + ("-negative" if negative else "-current")
                unit = work / (case + ".c")
                text = source.read_text()
                marker = "    (void)yynerrs; /* User actions may omit the error count. */\n"
                if text.count(marker) != 1:
                    raise RuntimeError("The generator does not contain the expected counter-use change")
                unit.write_text(text.replace(marker, "") if negative else text)
                flags = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
                         "-Werror=unused-but-set-variable", "-fsanitize=address,undefined",
                         "-fno-sanitize-recover=all"]
                flags += shlex.split(os.environ.get("CFLAGS", ""))
                binary = work / case
                result = run(case + "-build", cc + flags + [str(unit), "-o", str(binary)])
                if negative and not reads_counter:
                    require(result.returncode != 0 and "unused-but-set-variable" in result.stdout,
                            "Missing-counter-use control did not fail for the expected reason")
                else:
                    require(result.returncode == 0, result.stdout)
                    require(run(case + "-run", [str(binary)]).returncode == 0, "Parser contract failed")
    print("Both parser skeletons preserve optional error counts and error recovery")


if __name__ == "__main__":
    main()
