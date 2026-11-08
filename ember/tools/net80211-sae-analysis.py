#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors. AI-assisted.
"""Analyze exact station SAE/lifetime fixture units and preprocessor variants."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def analyze(database, style=False):
    entries = json.loads(database.read_text())
    if not entries:
        raise ValueError(f"empty compilation database: {database}")
    for index, entry in enumerate(entries):
        arguments = iter(entry["arguments"][1:])
        source_flags = []
        for argument in arguments:
            if argument == "-o":
                next(arguments)
            elif argument == "-c":
                # Analysis/preprocessing select their own compiler action.
                continue
            else:
                source_flags.append(argument)
        # GCC's analyzer is run by the fixture compiler. Clang uses its own
        # analyzer with the same remaining compilation flags.
        flags = [flag for flag in source_flags if flag != "-fanalyzer"]
        output = database.parent / f"clang-analysis-{index}.plist"
        subprocess.run([os.environ.get("CSA") or "clang", "--analyze",
                        "-Xanalyzer", "-analyzer-werror"] + flags +
                       ["-o", str(output.resolve())],
                       cwd=entry["directory"], check=True)
        # Cppcheck cannot infer compiler builtin macros or implicit native
        # system include paths from the database. Preserve the real compiler's
        # header and endian selections instead of inventing platform defines.
        preprocessed = database.parent / f"cppcheck-source-{index}.i"
        subprocess.run([entry["arguments"][0]] + source_flags +
                       ["-E", "-o", str(preprocessed.resolve())],
                       cwd=entry["directory"], check=True)
        categories = "warning,performance,portability"
        if style:
            categories += ",style"
        standard = next((flag[5:] for flag in reversed(source_flags)
                         if flag.startswith("-std=")), "c99")
        subprocess.run(["cppcheck", "--enable=" + categories,
                        "--check-level=exhaustive",
                        "--error-exitcode=1", "--std=" + standard,
                        "--language=c", str(preprocessed.resolve())], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--style", action="store_true",
                        help="add advisory style diagnostics; findings remain nonzero")
    parser.add_argument("database", nargs="+", type=Path)
    args = parser.parse_args()
    for database in args.database:
        analyze(database, args.style)
