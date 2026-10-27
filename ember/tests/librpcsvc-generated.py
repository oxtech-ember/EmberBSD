#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Generate all librpcsvc protocols and compile their actual target serializers.

Select the native rpcgen/CPP and target compiler/sysroot explicitly. Repeat
--cpp-argument for each preprocessor argument. Optional --baseline names the
old rnusers.x; its generated serializer must fail the prototype diagnostic.
This checks generation and target compilation, not network or target execution.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("rpcgen", "cpp", "cc", "sysroot", "work"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--target", required=True)
    parser.add_argument("--cpp-argument", action="append", default=[])
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    for name in ("rpcgen", "cpp", "cc"):
        value = getattr(args, name)
        if not value.is_absolute() or not value.is_file():
            parser.error("--" + name + " must select an existing absolute executable")
    if not args.sysroot.is_absolute() or not args.sysroot.is_dir():
        parser.error("--sysroot must select existing absolute target headers")
    args.work.mkdir(parents=True, exist_ok=False)
    work = args.work.resolve()
    root = Path(__file__).resolve().parents[2]
    env = dict(os.environ, RPCGEN_CPP=str(args.cpp))
    commands = []
    generator = [str(args.rpcgen)]
    for argument in args.cpp_argument:
        generator += ["-X", argument]
    generator += ["-B"]
    compiler = [str(args.cc), "--no-default-config", "--target=" + args.target,
                "--sysroot=" + str(args.sysroot), "-std=gnu11", "-O2", "-fPIC",
                "-Wall", "-Wextra", "-Werror", "-Wstrict-prototypes",
                "-Wmissing-prototypes", "-Wdeprecated-non-prototype"]

    def run(argv, directory, *, baseline=False):
        result = subprocess.run(argv, cwd=directory, env=env, capture_output=True,
                                text=True, timeout=30)
        commands.append(dict(argv=argv, directory=str(directory), status=result.returncode,
                             output=result.stdout + result.stderr))
        (work / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if baseline:
            if result.returncode == 0 or "deprecated-non-prototype" not in result.stderr:
                raise RuntimeError("old rnusers source did not reproduce the prototype error")
        elif result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def generate(source, directory, *, baseline=False):
        name = source.stem
        run(generator + ["-h", str(source), "-o", name + ".h"], directory)
        run(generator + ["-c", str(source), "-o", name + "_xdr.c"], directory)
        run(compiler + ["-c", name + "_xdr.c", "-o", name + ".o"],
            directory, baseline=baseline)

    sources = sorted((root / "lib/librpcsvc").glob("*.x"))
    if not sources:
        raise RuntimeError("librpcsvc protocol sources are absent")
    for source in sources:
        generate(source, work)
        print(source.name + ": PASS", flush=True)
    if args.baseline:
        directory = work / "baseline"
        directory.mkdir()
        source = directory / "rnusers.x"
        shutil.copyfile(args.baseline, source)
        generate(source, directory, baseline=True)
        print("old rnusers prototype failure reproduced: PASS", flush=True)


if __name__ == "__main__":
    main()
