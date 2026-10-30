#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Extract and check the production OFIOCNEXTPROP case and OF_nextprop."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess


def balanced_block(text, start):
    opening = text.find("{", start)
    if opening < 0:
        raise ValueError("missing opening brace")
    depth = 0
    state = "code"
    i = opening
    while i < len(text):
        char = text[i]
        next_char = text[i + 1] if i + 1 < len(text) else ""
        if state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and next_char == "/":
                state = "code"
                i += 1
        elif state in ("string", "char"):
            if char == "\\":
                i += 1
            elif ((state == "string" and char == '"') or
                  (state == "char" and char == "'")):
                state = "code"
        elif char == "/" and next_char == "/":
            state = "line-comment"
            i += 1
        elif char == "/" and next_char == "*":
            state = "block-comment"
            i += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "char"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return opening, i + 1
        i += 1
    raise ValueError("unterminated brace block")


def extract_function(source, name):
    marker = f"{name}("
    positions = [i for i in range(len(source)) if source.startswith(marker, i)]
    starts = []
    for pos in positions:
        name_line = source.rfind("\n", 0, pos) + 1
        return_line = source.rfind("\n", 0, name_line - 1) + 1
        if source[return_line:name_line].strip() == "int":
            starts.append(return_line)
    if len(starts) != 1:
        raise ValueError(
            f"expected one definition for {name}, found {len(starts)}")
    opening, end = balanced_block(source, starts[0])
    return starts[0], end, source[starts[0]:end]


def extract_case(source):
    start = source.find("\tcase OFIOCNEXTPROP: {")
    if start < 0:
        raise ValueError("missing OFIOCNEXTPROP case")
    opening, end = balanced_block(source, start)
    return start, end, source[start:end]


def run(command, log_path, env=None, expected=0):
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                env=env, text=True)
    if result.returncode != expected:
        raise SystemExit(
            f"FAIL: command returned {result.returncode}; see {log_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mode", choices=("normal", "asan", "sa", "cppcheck"),
                        default="normal")
    parser.add_argument("--case", choices=("empty", "long", "all"),
                        default="all")
    args = parser.parse_args()
    root = args.source.resolve()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit(f"refusing existing output directory: {output}")
    cc = shlex.split(os.environ.get("CC", ""))
    if not cc:
        raise SystemExit("select the SDK Clang compiler through CC")

    ioctl_path = root / "sys/dev/ofw/openfirmio.c"
    fdt_path = root / "sys/dev/fdt/fdt_openfirm.c"
    fixture_path = root / "ember/tools/openfirm-nextprop-contract.c"
    inputs = {str(path.relative_to(root)): path.read_bytes()
              for path in (ioctl_path, fdt_path, fixture_path)}
    ioctl_text = inputs[str(ioctl_path.relative_to(root))].decode()
    fdt_text = inputs[str(fdt_path.relative_to(root))].decode()
    fixture = inputs[str(fixture_path.relative_to(root))].decode()
    case_start, case_end, case = extract_case(ioctl_text)
    fn_start, fn_end, function = extract_function(fdt_text, "OF_nextprop")
    if fixture.count("/* ACTUAL_IOCTL_CASE */") != 1 or \
            fixture.count("/* ACTUAL_OF_NEXTPROP */") != 1:
        raise SystemExit("fixture extraction markers must each occur once")

    output.mkdir(parents=True)
    generated = fixture.replace("/* ACTUAL_IOCTL_CASE */", case).replace(
        "/* ACTUAL_OF_NEXTPROP */", function)
    unit = output / "openfirm-nextprop-generated.c"
    unit.write_text(generated, encoding="utf-8")
    manifest = {
        "mode": args.mode,
        "case": args.case,
        "compiler": cc,
        "sources": {
            name: hashlib.sha256(data).hexdigest()
            for name, data in inputs.items()
        },
        "extraction": {
            "ioctl_case": {"source": "sys/dev/ofw/openfirmio.c",
                            "first_byte": case_start, "end_byte": case_end,
                            "sha256": hashlib.sha256(case.encode()).hexdigest()},
            "backend_function": {"source": "sys/dev/fdt/fdt_openfirm.c",
                                 "first_byte": fn_start, "end_byte": fn_end,
                                 "sha256": hashlib.sha256(
                                     function.encode()).hexdigest()},
        },
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    flags = ["-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror"]
    if args.mode == "asan":
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    binary = output / "openfirm-nextprop-contract"
    if args.mode in ("normal", "asan"):
        run(cc + flags + [str(unit), "-o", str(binary)], output / "compile.log")
        env = os.environ.copy()
        if args.mode == "asan":
            env["ASAN_OPTIONS"] = "abort_on_error=1:detect_leaks=1"
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        run([str(binary), args.case], output / "runtime.log", env=env)
    elif args.mode == "sa":
        run(cc + flags + ["--analyze", "-Xanalyzer", "-analyzer-output=text",
                          "-Xanalyzer", "-analyzer-werror", str(unit),
                          "-o", str(output / "analysis.plist")],
            output / "static-analyzer.log")
    else:
        cppcheck = shlex.split(os.environ.get("CPPCHECK", ""))
        if not cppcheck:
            raise SystemExit(
                "select the SDK Cppcheck executable through CPPCHECK")
        run(cppcheck + ["--enable=warning,performance,portability",
                        "--check-level=exhaustive", "--error-exitcode=1",
                        "--std=c11", "--platform=unix64", str(unit)],
            output / "cppcheck.log")
    print("PASS: openfirm OFIOCNEXTPROP "
          f"{args.mode}/{args.case}; evidence {output}")


if __name__ == "__main__":
    main()
