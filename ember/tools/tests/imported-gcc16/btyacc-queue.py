#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""AI-assisted realloc failures for both btyacc lexical-queue growth sites."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


def enlarge_blocks(text):
    starts = list(re.finditer(
        r"^[ \t]*size_t p = \(size_t\) \(yylvp - yylvals\);", text, re.M))
    blocks = []
    for start in starts:
        end = re.search(r"yylplim = yylpsns \+ s;\n#endif", text[start.start():])
        if end is None:
            raise RuntimeError("lexical growth site does not retarget locations")
        blocks.append(text[start.start():start.start() + end.end()])
    if len(blocks) != 2:
        raise RuntimeError(f"skeleton has {len(blocks)} lexical growth sites")
    return blocks


def embedded_blocks(text):
    blocks = []
    start = 0
    while True:
        at = text.find("Enlarge lexical value queue", start)
        if at < 0:
            break
        lines = []
        started = False
        saw_limit = False
        for line in text[at:].splitlines():
            match = re.match(r'\s*"(.*)"\s*,\s*$', line)
            if not match:
                if started:
                    break
                continue
            body = match.group(1).replace(r"\"", '"').replace(r"\\", "\\")
            if "size_t p = " in body:
                started = True
            if not started:
                continue
            lines.append(body)
            if "yylplim = yylpsns + s;" in body:
                saw_limit = True
            if saw_limit and body.strip() == "#endif":
                break
        if not saw_limit or lines[-1].strip() != "#endif":
            raise RuntimeError("embedded growth site is incomplete")
        blocks.append("\n".join(lines))
        start = at + 1
    if len(blocks) != 2:
        raise RuntimeError(f"embedded skeleton has {len(blocks)} lexical growth sites")
    return blocks


def wrap(name, body):
    return f"static int\n{name}(void)\n{{\n{body}\n\treturn 0;\nyyenomem:\n\treturn 1;\n}}\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_tree", type=Path, help="prepared common Ports byacc source")
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--mode", choices=("plain", "sanitize", "analyze"), default="sanitize")
    args = parser.parse_args()
    root = args.source_tree.resolve(strict=True)
    fixture = Path(__file__).resolve().parent
    cc = shlex.split(os.environ.get("CC", "gcc-16"))
    version = subprocess.check_output(cc + ["-dumpfullversion"], text=True).strip()
    if version.split(".")[0] != "16":
        parser.error("Use GCC 16")
    if args.work.exists():
        parser.error("Output directory already exists")
    work = args.work.resolve()
    work.mkdir(parents=True)
    skel = (root / "btyaccpar.skel").read_text()
    embedded = (root / "btyaccpar.c").read_text()
    growth = re.search(r"#ifndef YYLVQUEUEGROWTH\n#define YYLVQUEUEGROWTH (\d+)\n#endif\n", skel)
    if growth is None:
        raise RuntimeError("YYLVQUEUEGROWTH is not defined by the skeleton")
    parts = []
    for index, body in enumerate(enlarge_blocks(skel)):
        parts.append(wrap(f"grow_skel_{index}", body))
    for index, body in enumerate(embedded_blocks(embedded)):
        parts.append(wrap(f"grow_embed_{index}", body))
    (work / "queue-sites.inc").write_text(
        f"#define YYLVQUEUEGROWTH {growth.group(1)}\n" + "\n".join(parts))
    receipt = {
        "compiler": version,
        "mode": args.mode,
        "inputs": {
            "btyaccpar.skel": hashlib.sha256(skel.encode()).hexdigest(),
            "btyaccpar.c": hashlib.sha256(embedded.encode()).hexdigest(),
        },
        "commands": [],
        "results": {},
    }

    def run(label, command, cwd):
        receipt["commands"].append(command)
        result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=120)
        (work / (label + ".log")).write_text(result.stdout)
        receipt["results"][label] = result.returncode
        (work / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(label, result.returncode, flush=True)
        return result.returncode

    failures = 0
    for locations in (False, True):
        name = "locations" if locations else "plain"
        flags = ["-std=gnu11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                 "-I" + str(work)]
        flags += shlex.split(os.environ.get("CFLAGS", ""))
        if locations:
            flags.append("-DYYLTYPE_IS_DECLARED")
        if args.mode == "sanitize":
            flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        if args.mode == "analyze":
            flags.append("-fanalyzer")
        binary = work / name
        if run(name + "-build", cc + flags + [str(fixture / "btyacc-queue.c"), "-o", str(binary)], work) != 0:
            failures += 1
            continue
        if args.mode == "analyze":
            check = ["cppcheck", "--enable=warning,performance,portability", "--std=c11",
                     "--template={severity}:{id}:{message}", "-I" + str(work)]
            if locations:
                check.append("-DYYLTYPE_IS_DECLARED")
            failures += run(name + "-cppcheck", check + [str(fixture / "btyacc-queue.c")], work) != 0
            findings = (work / (name + "-cppcheck.log")).read_text()
            if re.search(r"^(error|warning|performance):", findings, re.M):
                failures += 1
        failures += run(name + "-run", [str(binary)], work) != 0
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
