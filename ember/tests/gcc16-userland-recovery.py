#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted actual-source userland regressions.
"""Run focused GCC16 host checks, with an optional pinned source baseline.

Example: python3 ember/tests/gcc16-userland-recovery.py --cc /path/gcc-16
  --baseline PRE_FIX_COMMIT --sanitize /absolute/new-work

Each fixture compiles the actual selected source functions. Controlled peers
replace the UI, command evaluator, device and callback environment. These are
host contracts, not full utility builds or target execution. The work directory
retains generated sources, compiler commands, diagnostics and results.json.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CASES = {
    "funopen": "lib/libc/stdio/funopen.c",
    "mail": "usr.bin/mail/cmd4.c",
    "tip": "usr.bin/tip/tipout.c",
    "hack": "games/hack/hack.topl.c",
    "larn": "games/larn/monster.c",
    "kgmon": "usr.sbin/kgmon/kgmon.c",
    "pac": "usr.sbin/lpr/pac/pac.c",
    "raid": "sbin/raidctl/raidctl.c",
    "csh": "bin/csh/glob.c",
}


def function(source, name):
    """Select a file-scope KNF function; nested closing braces are indented."""
    match = re.search(r"^" + re.escape(name) + r"\(", source, re.MULTILINE)
    if match is None:
        raise ValueError("missing function: " + name)
    start = source.rfind("\n\n", 0, match.start()) + 2
    opening = source.index("\n{", match.end())
    end = source.index("\n}", opening) + 2
    return source[start:end]


def match_one(source, pattern):
    matches = re.findall(pattern, source, re.MULTILINE | re.DOTALL)
    if len(matches) != 1:
        raise ValueError("expected one source region: " + pattern)
    return matches[0]


def fragments(case, source):
    if case == "funopen":
        return {"FUNOPEN": function(source, "cwritefn")}
    if case == "mail":
        return {"MAIL": function(source, "free_name") + "\n" + function(source, "delsmopts")}
    if case == "tip":
        return {"TIP": function(source, "intEMT")}
    if case == "hack":
        return {"HACK": function(source, "vpline")}
    if case == "larn":
        table = match_one(source, r"(static char\s+nobjtab\[\].*?\n\s*OLONGSWORD};)")
        return {"LARN": table + "\n" + function(source, "newobject")}
    if case == "kgmon":
        return {
            "KGMON_DECL": match_one(source, r"(char on_cpu\[[^\n]+;)"),
            "KGMON_FORMAT": match_one(source, r'(snprintf\(on_cpu,[^\n]+;)'),
        }
    if case == "pac":
        return {"PAC": function(source, "dumpit")}
    if case == "raid":
        table = match_one(source, r"(#define STAR_MIDPOINT[^\n]+\nconst char stars\[\].*?;)")
        return {"RAID": table + "\n" + function(source, "get_bar")}
    if case == "csh":
        return {"CSH": "\n".join(function(source, name) for name in ("dobackp", "psave", "pword"))}
    raise ValueError(case)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("HOST_GCC16", "gcc-16"))
    parser.add_argument("--baseline", help="Compare actual sources from this local Git revision")
    parser.add_argument("--sanitize", action="store_true", help="Enable GCC ASan and UBSan")
    parser.add_argument("--case", choices=CASES, action="append", dest="cases")
    parser.add_argument("work", type=Path)
    args = parser.parse_args()
    work = args.work.expanduser().absolute()
    if not args.work.is_absolute() or work.exists() or not work.parent.is_dir():
        parser.error("work must be a new absolute directory with an existing parent")
    version = subprocess.check_output([args.cc, "-dumpfullversion"], text=True).strip()
    if not version.startswith("16."):
        parser.error("a real GCC16 host compiler is required")
    work.mkdir()
    template = Path(__file__).with_suffix(".c").read_text()
    flags = shlex.split(os.environ.get("HOST_GCC16_FLAGS", ""))
    flags += ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wdangling-pointer=2"]
    results = {"compiler": args.cc, "version": version, "flags": flags,
               "baseline": args.baseline, "scope": "actual-source host contracts", "cases": {}}
    compdb = []
    failed = False
    for case in args.cases or CASES:
        results["cases"][case] = {}
        for state in ("candidate", "baseline") if args.baseline else ("candidate",):
            path = CASES[case]
            if state == "baseline":
                source = subprocess.check_output(["git", "show", args.baseline + ":" + path], cwd=ROOT).decode()
            else:
                source = (ROOT / path).read_text()
            candidate = template
            for key, text in fragments(case, source).items():
                marker = "/* @" + key + "@ */"
                if candidate.count(marker) != 1:
                    raise ValueError("missing fixture slot: " + key)
                candidate = candidate.replace(marker, text)
            # Preserve the original copyright and license with the extracted code.
            candidate = source[:source.index("#include")] + "\n" + candidate
            stem = work / (case + "-" + state)
            src, exe = stem.with_suffix(".c"), stem.with_suffix(".bin")
            src.write_text(candidate)
            command = [args.cc, *flags, "-DTEST_" + case.upper(), str(src), "-o", str(exe)]
            build = subprocess.run(command, text=True, capture_output=True)
            stem.with_suffix(".build.log").write_text(build.stdout + build.stderr)
            compdb.append({"directory": str(work), "file": str(src), "arguments": command})
            result = {"source": path, "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
                      "compile_exit": build.returncode, "compile_command": command}
            if build.returncode == 0:
                if args.sanitize:
                    # Keep plain warning checks separate: ASan instrumentation
                    # changes GCC's dangling-pointer diagnostic in dobackp.
                    command = [args.cc, *flags, "-fsanitize=address,undefined",
                               "-fno-omit-frame-pointer", "-DTEST_" + case.upper(),
                               str(src), "-o", str(exe)]
                    sanitized = subprocess.run(command, text=True, capture_output=True)
                    stem.with_suffix(".sanitize.log").write_text(sanitized.stdout + sanitized.stderr)
                    result["sanitize_command"] = command
                    result["sanitize_compile_exit"] = sanitized.returncode
                    if sanitized.returncode != 0:
                        failed = True
                        result["pass"] = False
                        result["error"] = "sanitizer build failed"
                        results["cases"][case][state] = result
                        continue
                run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=20)
                stem.with_suffix(".run.log").write_text(run.stdout + run.stderr)
                result["run_exit"] = run.returncode
                result["pass"] = run.returncode == 0
            else:
                result["pass"] = False
            results["cases"][case][state] = result
            if state == "candidate" and not result["pass"]:
                failed = True
            if state == "baseline" and result["pass"]:
                failed = True
                result["error"] = "baseline did not reproduce the selected regression"
            if state == "baseline" and build.returncode != 0:
                diagnostic = {"csh": "-Werror=dangling-pointer", "raid": "-Werror=format-truncation"}.get(case)
                if diagnostic is None or diagnostic not in build.stderr:
                    failed = True
                    result["error"] = "unexpected baseline build failure"
            print(case, state, "PASS" if result["pass"] else "FAIL", flush=True)
    (work / "compile_commands.json").write_text(json.dumps(compdb, indent=2) + "\n")
    (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
