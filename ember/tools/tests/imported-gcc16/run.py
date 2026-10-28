#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""AI-assisted actual-source regressions for imported IPFilter and nvi fixes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


def extract(pattern, source):
    matches = list(re.finditer(pattern, source, re.M | re.S))
    if len(matches) != 1:
        raise ValueError(f"Expected one source match for {pattern!r}; got {len(matches)}")
    return matches[0].group(0) + "\n"


def read_nvi_source(root, selected, revision, path):
    """Read historical OS source or the explicitly selected common source."""
    if revision:
        return subprocess.check_output(["git", "-C", str(root), "show",
                                        revision + ":external/bsd/nvi/dist/" + path])
    if not selected:
        raise ValueError("--nvi-source or EMBER_NVI_SOURCE is required for nvi cases")
    return (Path(selected).resolve(strict=True) / path).read_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_tree", type=Path)
    parser.add_argument("--work", required=True, type=Path,
                        help="New output directory; existing directories are rejected")
    parser.add_argument("--nvi-source", type=Path, default=os.environ.get("EMBER_NVI_SOURCE"),
                        help="Prepared common Ports nvi source (or EMBER_NVI_SOURCE)")
    parser.add_argument("--revision", help="Read this Git revision for a causal comparison")
    parser.add_argument("--case", choices=("all", "pcap", "pcap-open", "icmp", "nvi", "nvi-space"),
                        default="all")
    parser.add_argument("--mode", choices=("plain", "sanitize", "analyze", "memcheck"), default="sanitize")
    args = parser.parse_args()
    root = args.source_tree.resolve(strict=True)
    fixture = Path(__file__).resolve().parent
    cc = shlex.split(os.environ.get("CC", "gcc-16"))
    version = subprocess.check_output(cc + ["-dumpfullversion"], text=True).strip()
    if int(version.split(".")[0]) != 16:
        parser.error("Use GCC 16; this regression does not qualify another compiler")
    if args.work.exists():
        parser.error("Output directory already exists")
    work = args.work.resolve()
    work.mkdir(parents=True)
    receipt = {"compiler": version, "mode": args.mode, "revision": args.revision,
               "inputs": {}, "commands": [], "results": {}}

    def source(path):
        data = (subprocess.check_output(["git", "-C", str(root), "show", args.revision + ":" + path])
                if args.revision else (root / path).read_bytes())
        receipt["inputs"][path] = hashlib.sha256(data).hexdigest()
        return data.decode("latin1")

    def nvi_source(path):
        try:
            data = read_nvi_source(root, args.nvi_source, args.revision, path)
        except ValueError as exc:
            parser.error(str(exc))
        receipt["inputs"]["nvi/" + path] = hashlib.sha256(data).hexdigest()
        return data.decode("latin1")

    def run(label, cmd):
        receipt["commands"].append(cmd)
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=120, cwd=work)
        (work / (label + ".log")).write_text(result.stdout)
        receipt["results"][label] = result.returncode
        (work / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(label, result.returncode, flush=True)
        return result.returncode

    cases = []
    if args.case in ("all", "pcap", "pcap-open", "icmp"):
        pcap = source("external/bsd/ipf/dist/lib/ipft_pc.c")
        icmp = source("external/bsd/ipf/dist/tools/ipmon.c")
        types = "typedef uint32_t u_32_t;\n"
        types += extract(r"^struct\s+llc\s*\{.*?^\} fileheader_t;", pcap)
        types += extract(r"^typedef struct \{\n\s*u_32_t.*?^\} packetheader_t;", pcap)
        types += extract(r"^#define\s+SWAPLONG\(y\).*?\n[^\n]+", pcap)
        types += extract(r"^#define\s+SWAPSHORT\(y\).*?\n[^\n]+", pcap)
        for name in ("icmp_subtype", "icmp_type"):
            types += extract(r"^typedef\s+struct\s+" + name + r"\s*\{.*?^\} " + name + r"_t;", icmp)
        (work / "ipf-types.inc").write_text(types)
        functions = pcap[:pcap.index('#include "ipf.h"')]
        functions += extract(r"^static\s+int\s+ipcap_read_rec\(rec\).*?^\}", pcap)
        for name in ("find_icmpsubtype", "find_icmptype"):
            functions += extract(r"^static icmp_[a-z]+_t \*\n" + name + r"\(.*?^\}", icmp)
        (work / "ipf-functions.inc").write_text(functions)
        (work / "ipf-open.inc").write_text(extract(r"^static\s+void\s+iswap_hdr\(p\).*?^\}", pcap) + extract(r"^static\s+int\s+ipcap_open\(fname\).*?^\}", pcap))
        cases.append(("ipf", fixture / "ipf.c", [],
                      [args.case] if args.case != "all" else ["pcap", "pcap-open", "icmp"]))
    if args.case in ("all", "nvi"):
        nvi = nvi_source("common/delete.c")
        (work / "nvi-function.inc").write_text(nvi[:nvi.index('#include "config.h"')] +
                                             extract(r"^int\ndel\(.*?^\}", nvi))
        cases += [("nvi-byte", fixture / "nvi.c", ["-DCHAR_T=unsigned char"], [None]),
                  ("nvi-wide", fixture / "nvi.c", [], [None])]
    if args.case in ("all", "nvi-space"):
        mem = nvi_source("common/mem.h")
        start = mem.index("#if defined(HAVE_GCC)")
        end = mem.index("ADD_SPACE_RETW(")
        end = mem.index("\n", end) + 1
        if mem[end - 2] == "\\":
            end = mem.index("\n", end) + 1
        (work / "nvi-space.inc").write_text(mem[start:end])
        cases.append(("nvi-space", fixture / "nvi-space.c", [], [None]))
    failures = 0
    for name, test, defines, arguments in cases:
        flags = ["-std=gnu11", "-g", "-O1", "-Wall", "-Wextra", "-I" + str(work)]
        flags += shlex.split(os.environ.get("CFLAGS", "")) + defines
        if args.mode == "sanitize":
            flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        if args.mode == "analyze":
            flags += ["-fanalyzer"]
        binary = work / name
        if run(name + "-build", cc + flags + [str(test), "-o", str(binary)]) != 0:
            failures += 1
            continue
        if args.mode == "analyze":
            # Keep all findings in the receipt; no category suppressions.
            failures += run(name + "-cppcheck", ["cppcheck", "--enable=all", "--inconclusive",
                "--std=c11", "--check-level=exhaustive", "--template={severity}:{id}:{message}", "-I" + str(work)] + defines + [str(test)]) != 0
            findings = (work / (name + "-cppcheck.log")).read_text()
            failures += bool(re.search(r"^(error|warning|performance):", findings, re.M))
        for arg in arguments:
            command = [str(binary)] + ([] if arg is None else [arg])
            if args.mode == "memcheck":
                command = ["valgrind", "--leak-check=full", "--show-leak-kinds=all",
                    "--errors-for-leak-kinds=all", "--track-origins=yes", "--error-exitcode=1"] + command
            failures += run(name + "-" + (arg or "run"), command) != 0
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
