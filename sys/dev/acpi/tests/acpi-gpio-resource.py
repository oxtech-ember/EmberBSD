#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the actual ACPI GPIO lookup functions against a freeing ACPICA double."""

import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def extract_function(source, name):
    pattern = re.compile(
        r"(?m)^(?:static\s+)?ACPI_STATUS\s*\n" + re.escape(name) + r"\(")
    matches = list(pattern.finditer(source))
    require(len(matches) == 1, f"expected one production definition of {name}")
    start = matches[0].start()
    end = source.find("\n}\n", start)
    require(end >= 0, f"unterminated production function {name}")
    return source[start:end + 3]


def make_fixture(source, template):
    begin = source.index("struct acpi_gpio_resource_context {")
    end = source.index("\n};", begin) + 3
    context = source[begin:end]
    names = ["acpi_gpio_translate", "acpi_gpio_parse", "acpi_gpio_get",
             "acpi_gpio_get_int", "acpi_gpio_get_io"]
    functions = "\n\n".join(extract_function(source, name) for name in names
                                  if re.search(r"(?m)^(?:static\s+)?ACPI_STATUS\s*\n" +
                                               re.escape(name) + r"\(", source))
    # The baseline has no shared lookup helper. Its public entry points still
    # compile into the same freeing-resource harness.
    required = ["acpi_gpio_translate", "acpi_gpio_parse",
                "acpi_gpio_get_int", "acpi_gpio_get_io"]
    for name in required:
        require(re.search(r"(?m)^(?:static\s+)?ACPI_STATUS\s*\n" +
                          re.escape(name) + r"\(", source) is not None,
                f"missing production function {name}")
    fixture = template.replace("@PRODUCTION_CONTEXT@", context)
    fixture = fixture.replace("@PRODUCTION_FUNCTIONS@", functions)
    require("@PRODUCTION_" not in fixture, "unexpanded production fixture marker")
    return fixture


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def compiler_matches_host(machine):
    machine = machine.lower()
    host_machine = platform.machine().lower()
    machine_aliases = {
        "arm64": ("arm64", "aarch64"),
        "aarch64": ("arm64", "aarch64"),
        "amd64": ("amd64", "x86_64"),
        "x86_64": ("amd64", "x86_64"),
    }
    aliases = machine_aliases.get(host_machine, (host_machine,))
    triplet = machine.split("-")
    if len(triplet) < 3 or triplet[0] not in aliases:
        return False
    host_system = platform.system().lower()
    system_aliases = {
        "darwin": ("darwin",),
        "netbsd": ("netbsd",),
        "freebsd": ("freebsd",),
        "openbsd": ("openbsd",),
        "linux": ("linux",),
    }
    os_component = triplet[2]
    return any(os_component.startswith(alias)
               for alias in system_aliases.get(host_system, ()))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        help="driver source file; defaults to ../acpi_gpio.c")
    parser.add_argument("--compile-only", action="store_true",
                        help="compile but never execute the generated binary")
    parser.add_argument("--output-dir", type=Path,
                        help="new directory for compile-only fixture, binary, and receipt")
    parser.add_argument("--expect-uaf", action="store_true",
                        help="require the baseline source to fail with ASan UAF")
    parser.add_argument("--expect-escape", action="store_true",
                        help="require baseline source to call a resource API after its callback")
    parser.add_argument("--no-sanitize", action="store_true",
                        help="use retained buffers and callback guards without sanitizer claims")
    args = parser.parse_args()
    require(args.compile_only == (args.output_dir is not None),
            "--compile-only requires --output-dir, which is not used for native runs")
    require(not (args.expect_uaf and args.no_sanitize),
            "--expect-uaf requires the sanitizer-enabled run")
    require(not (args.expect_uaf and args.expect_escape),
            "choose one baseline failure mode")
    require(not args.expect_escape or args.no_sanitize,
            "--expect-escape requires --no-sanitize")
    require(not args.compile_only or not (args.expect_uaf or args.expect_escape),
            "compile-only mode cannot run a baseline control")

    here = Path(__file__).resolve().parent
    source_path = args.source or here.parent / "acpi_gpio.c"
    source_path = source_path.resolve()
    source_bytes = source_path.read_bytes()
    source = source_bytes.decode("utf-8")
    template = (here / "acpi-gpio-resource.c").read_text(encoding="utf-8")
    fixture = make_fixture(source, template)

    cc = os.environ.get("HOST_CC", "")
    require(cc and Path(cc).is_absolute() and Path(cc).is_file(),
            "set HOST_CC to an absolute GCC 16 executable")
    version = subprocess.check_output([cc, "-dumpfullversion"], text=True).strip()
    require(version.split(".")[0] == "16", "the contract requires GCC 16")
    machine = subprocess.check_output([cc, "-dumpmachine"], text=True).strip()
    require(args.compile_only or compiler_matches_host(machine),
            "compiler target does not match this host; use --compile-only --output-dir NEW_DIR")
    flags = ["-O1", "-g", "-std=c11", "-Wall", "-Wextra", "-Werror"]
    if args.no_sanitize:
        flags.append("-DFIXTURE_RETAIN_RESOURCES")
    else:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    flags += shlex.split(os.environ.get("HOST_CFLAGS", ""))

    if args.compile_only:
        output_dir = args.output_dir
        require(output_dir.is_absolute(), "--output-dir must be an absolute path")
        require(not output_dir.exists() and not output_dir.is_symlink(),
                "--output-dir must be a new directory")
        output_dir.mkdir()
        output_dir = output_dir.resolve()
        cfile = output_dir / "fixture.c"
        binary = output_dir / "acpi-gpio-resource"
        cfile.write_text(fixture, encoding="utf-8")
        command = [cc, *flags, str(cfile), "-o", str(binary)]
        compiler_path = Path(cc).resolve()
        metadata = {
            "format": "emberbsd-acpi-gpio-resource-build",
            "version": 1,
            "mode": "compile-only",
            "source": str(source_path),
            "source_sha256": hashlib.sha256(source_bytes).hexdigest(),
            "fixture": "fixture.c",
            "fixture_sha256": sha256_file(cfile),
            "binary": "acpi-gpio-resource",
            "compiler": {
                "invocation": cc,
                "resolved_path": str(compiler_path),
                "sha256": sha256_file(compiler_path),
                "version": version,
                "machine": machine,
            },
            "flags": flags,
            "command": command,
            "execution": "not-run",
        }
        build = subprocess.run(command, capture_output=True, text=True,
                               encoding="utf-8", errors="replace", timeout=120)
        (output_dir / "compile.stdout").write_text(build.stdout, encoding="utf-8")
        (output_dir / "compile.stderr").write_text(build.stderr, encoding="utf-8")
        metadata["compile_exit_status"] = build.returncode
        if build.returncode == 0:
            metadata["binary_sha256"] = sha256_file(binary)
        write_json(output_dir / "build.json", metadata)
        require(build.returncode == 0,
                "compile-only build failed; inspect " + str(output_dir / "compile.stderr"))
        print("ACPI GPIO fixture compiled; target binary not executed")
        print("Build receipt: " + str(output_dir / "build.json"))
        return

    with tempfile.TemporaryDirectory(prefix="ember-acpi-gpio-resource-") as directory:
        work = Path(directory)
        cfile = work / "fixture.c"
        binary = work / "fixture"
        cfile.write_text(fixture, encoding="utf-8")
        command = [cc, *flags, str(cfile), "-o", str(binary)]
        build = subprocess.run(command, capture_output=True, text=True, timeout=120)
        require(build.returncode == 0, "compile failed:\n" + build.stdout + build.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        output = run.stdout + run.stderr
        if args.expect_uaf:
            require(run.returncode != 0 and
                    "AddressSanitizer: heap-use-after-free" in output,
                    "baseline did not reproduce the resource-buffer use-after-free:\n" + output)
            print("ACPI GPIO baseline: PASS (ASan reproduced heap-use-after-free)")
        elif args.expect_escape:
            require(run.returncode != 0 and
                    "namespace lookup escaped the resource callback" in output,
                    "baseline did not reproduce resource access after the callback:\n" + output)
            print("ACPI GPIO baseline: PASS (callback lifetime guard rejected escaped access)")
        else:
            require(run.returncode == 0, "execution failed:\n" + output)
            require("ACPI GPIO resource lifetime: PASS (12 cases)" in run.stdout,
                    "missing contract PASS marker")
            mode = "callback guard" if args.no_sanitize else "ASan/UBSan"
            print("ACPI GPIO resource lifetime: PASS (GCC " + version + "; " + mode + ")")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
