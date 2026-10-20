#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; actual-source stack-protector regression.
"""Check nlist allocation selection with GCC16; see nlist-ssp.md."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/libc/gen/nlist_aout.c"
OLD = "#if defined(__SSP__) || defined(__SSP_ALL__)"
NEW = OLD + " || defined(__SSP_STRONG__)"
MODES = ("-fno-stack-protector", "-fstack-protector",
         "-fstack-protector-all", "-fstack-protector-strong")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(command, cwd=None):
    result = subprocess.run(command, cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return {"command": command, "cwd": str(cwd) if cwd else None,
            "returncode": result.returncode, "output": result.stdout}


def gcc16(compiler):
    result = run([compiler, "-dumpfullversion"])
    require(result["returncode"] == 0 and
            result["output"].strip().split(".")[0] == "16",
            "select a GCC16 compiler")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True, help="Native GCC16")
    parser.add_argument("--target-command", type=Path,
                        help="JSON argv array from the actual libc C compile")
    parser.add_argument("--target-cwd", type=Path,
                        help="Working directory for that compile command")
    parser.add_argument("work", type=Path, help="New retained output directory")
    args = parser.parse_args()
    require(bool(args.target_command) == bool(args.target_cwd),
            "supply both target command and working directory")
    gcc16(args.cc)
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    source = SOURCE.read_text()
    require(source.count(NEW) == 2, "expected the two allocation/release guards")
    baseline = source.replace(NEW, OLD)
    start = source.index("\nint\n__fdnlist_aout(") + 1
    end = source.index("\n}\n", start) + 2
    function = source[start:end]
    fixture = Path(__file__).with_suffix(".c").read_text()
    # Keep the source's original license notices with the extracted function.
    licenses = source[:source.index("#include <sys/cdefs.h>")]
    host_source = work / "host.c"
    host_source.write_text(licenses + fixture.replace("/* @ACTUAL_FUNCTION@ */",
                                                    function))
    results = []
    for mode in MODES:
        output = work / mode.removeprefix("-f")
        command = [args.cc, "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-function", mode, str(host_source), "-o", str(output)]
        result = run(command)
        results.append(result)
        require(result["returncode"] == 0, result["output"])
        result = run([str(output)])
        results.append(result)
        require(result["returncode"] == 0, result["output"])
    # A release-only regression must fail at runtime, independently of warnings.
    broken = work / "missing-release.c"
    broken.write_text(host_source.read_text().replace(
        NEW + "\n\tfree(scoreboard);", OLD + "\n\tfree(scoreboard);"))
    broken_binary = work / "missing-release"
    result = run([args.cc, "-std=gnu11", "-O2", "-Wno-unused-function",
                  "-fstack-protector-strong", str(broken), "-o", str(broken_binary)])
    results.append(result)
    require(result["returncode"] == 0, result["output"])
    result = run([str(broken_binary)])
    results.append(result)
    require(result["returncode"] != 0, "missing-release mutation was accepted")

    if args.target_command:
        original = json.loads(args.target_command.read_text())
        require(isinstance(original, list) and all(isinstance(x, str) for x in original),
                "target command must be a JSON argv array")
        gcc16(original[0])
        inputs = [i for i, x in enumerate(original) if x.endswith("/nlist_aout.c")]
        require(len(inputs) == 1 and original.count("-o") == 1 and "-c" in original,
                "expected one nlist source and one compile output")
        require("-Werror" in original and "-Wstack-protector" in original,
                "keep the production stack-protector diagnostic")
        input_index = inputs[0]
        include = str(Path(original[input_index]).parent)
        for variant, text in (("current", source), ("baseline", baseline)):
            path = work / (variant + ".c")
            path.write_text(text)
            for mode in MODES:
                command = original.copy()
                command[input_index] = str(path)
                command[command.index("-o") + 1] = str(work / (
                    variant + mode + ".o"))
                command = [x for x in command if not re.fullmatch(
                    r"-f(?:no-)?stack-protector(?:-all|-strong)?", x)]
                command += [mode, "-I" + include]
                result = run(command, args.target_cwd)
                results.append(result)
                fail = variant == "baseline" and mode == "-fstack-protector-strong"
                if fail:
                    require(result["returncode"] != 0 and
                            "variable length buffer" in result["output"] and
                            "-Werror=stack-protector" in result["output"],
                            "baseline did not reproduce the intended diagnostic")
                else:
                    require(result["returncode"] == 0, result["output"])
    (work / "results.json").write_text(json.dumps({
        "source_sha256": hashlib.sha256(source.encode()).hexdigest(),
        "scope": "host function execution; optional target TU compile only",
        "target_executed": False, "results": results}, indent=2) + "\n")
    print("PASS four SSP modes, lookup/release and missing-release mutation" +
          ("; eight full target TU compiles have expected outcomes"
           if args.target_command else ""))


if __name__ == "__main__":
    main()
