#!/usr/bin/env python3
# Copyright (c) 2026 EmberBSD contributors.
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD libelf/GElf ABI regression.
"""Build and run the libelf/GElf ABI probe with an explicit GCC 16 toolchain."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import stat
import subprocess
import sys
import tempfile


SOURCE = Path(__file__).with_name("libelf-abi.c")
BASE_PAYLOADS = {
    "usr/lib/libelf.so.2.0",
    "usr/lib/libelf.a",
    "usr/include/libelf.h",
    "usr/include/gelf.h",
    "usr/include/elfdefinitions.h",
    "usr/include/sys/elfdefinitions.h",
}


def run(argv, *, cwd=None):
    return subprocess.run(argv, cwd=cwd, text=True, capture_output=True)


def verify_expected_base(sysroot, manifest_path):
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read expected base manifest: {error}") from error
    files = manifest.get("files")
    if not isinstance(files, dict) or not files:
        raise ValueError("expected base manifest has no files object")
    if set(files) != BASE_PAYLOADS:
        raise ValueError(
            "expected base manifest must cover exactly the libelf library and "
            "four public header payloads"
        )

    for relative, expected in files.items():
        relpath = PurePosixPath(relative)
        if relpath.is_absolute() or ".." in relpath.parts:
            raise ValueError(f"unsafe path in expected manifest: {relative!r}")
        if not relpath.parts:
            raise ValueError("empty path in expected base manifest")
        if not isinstance(expected, dict):
            raise ValueError(f"invalid expected record for {relative}")
        wanted_hash = expected.get("sha256")
        wanted_size = expected.get("size")
        if (not isinstance(wanted_hash, str) or
                not re.fullmatch(r"[0-9a-f]{64}", wanted_hash)):
            raise ValueError(f"invalid SHA256 for {relative}")
        if not isinstance(wanted_size, int) or wanted_size < 0:
            raise ValueError(f"invalid size for {relative}")

        path = sysroot
        for component in relpath.parts:
            path = path / component
            try:
                metadata = path.lstat()
            except OSError as error:
                raise ValueError(
                    f"missing expected base file {relative}: {error}"
                ) from error
            if stat.S_ISLNK(metadata.st_mode):
                raise ValueError(f"expected base path contains a symlink: {relative}")
        if not stat.S_ISREG(metadata.st_mode):
            raise ValueError(f"expected base payload is not a regular file: {relative}")
        digest = hashlib.sha256()
        size = 0
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                size += len(chunk)
                digest.update(chunk)
        if size != wanted_size or digest.hexdigest() != wanted_hash:
            raise ValueError(
                f"base payload mismatch for {relative}: size={size}, "
                f"sha256={digest.hexdigest()}"
            )
        print(f"PASS expected base payload {relative}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True,
                        help="absolute path to the GCC 16 target compiler")
    parser.add_argument("--sysroot", type=Path,
                        help="target sysroot for compilation and optional hash checks")
    parser.add_argument("--expected-base", type=Path,
                        help="optional JSON manifest of expected installed base files")
    parser.add_argument("--include-dir", action="append", default=[], type=Path,
                        help="additional target include directory; repeat as needed")
    parser.add_argument("--library-dir", action="append", default=[], type=Path,
                        help="additional target library directory; repeat as needed")
    parser.add_argument("--cflag", action="append", default=[],
                        help="one additional compiler argument; repeat as needed")
    parser.add_argument("--ldflag", action="append", default=[],
                        help="one additional linker argument; repeat as needed")
    parser.add_argument("--runner",
                        help="optional target runner command and arguments")
    parser.add_argument("--work-parent", type=Path,
                        help="parent directory for temporary compiler outputs")
    args = parser.parse_args()

    cc = Path(args.cc).expanduser()
    if not cc.is_absolute():
        parser.error("--cc must be an absolute executable path")
    if not cc.is_file() or not cc.stat().st_mode & 0o111:
        parser.error(f"--cc is not an executable file: {cc}")
    if args.expected_base is not None and args.sysroot is None:
        parser.error("--expected-base requires --sysroot")
    if args.sysroot is not None and not args.sysroot.is_dir():
        parser.error(f"--sysroot is not a directory: {args.sysroot}")
    if args.work_parent is not None and not args.work_parent.is_dir():
        parser.error(f"--work-parent is not a directory: {args.work_parent}")
    if not SOURCE.is_file():
        parser.error(f"probe source is missing: {SOURCE}")

    try:
        if args.expected_base is not None:
            verify_expected_base(args.sysroot, args.expected_base)

        banner = run([str(cc), "--version"])
        banner_line = banner.stdout.splitlines()[0] if banner.stdout else ""
        if (banner.returncode != 0 or "gcc" not in banner_line.lower() or
                "clang" in banner_line.lower()):
            raise ValueError(
                f"compiler banner does not identify GCC: {banner_line!r}; "
                f"stderr={banner.stderr.strip()!r}"
            )
        target = run([str(cc), "-dumpmachine"])
        target_triplet = target.stdout.strip()
        if target.returncode != 0 or "netbsd" not in target_triplet.lower():
            raise ValueError(
                f"compiler target is not a NetBSD triplet: "
                f"status={target.returncode}, output={target_triplet!r}, "
                f"stderr={target.stderr.strip()!r}"
            )
        version = run([str(cc), "-dumpfullversion"])
        version_text = version.stdout.strip()
        if version.returncode != 0 or not re.fullmatch(r"16(?:\..*)?", version_text):
            raise ValueError(
                f"compiler must report GCC major 16 with -dumpfullversion; "
                f"status={version.returncode}, output={version_text!r}, "
                f"stderr={version.stderr.strip()!r}"
            )
        print(f"GCC banner: {banner_line}")
        print(f"GCC target: {target_triplet}")
        print(f"GCC version: {version_text}")

        work_parent = args.work_parent
        with tempfile.TemporaryDirectory(prefix="emberbsd-libelf-abi-",
                                         dir=work_parent) as directory:
            work = Path(directory)
            executable = work / "libelf-abi"
            compile_argv = [str(cc), "-std=c11", "-Wall", "-Wextra", "-Werror"]
            if args.sysroot is not None:
                compile_argv.append(f"--sysroot={args.sysroot.resolve()}")
            for include_dir in args.include_dir:
                compile_argv.append(f"-I{include_dir.resolve()}")
            compile_argv.extend(args.cflag)
            compile_argv.extend([str(SOURCE), "-o", str(executable)])
            for library_dir in args.library_dir:
                compile_argv.append(f"-L{library_dir.resolve()}")
            compile_argv.extend(args.ldflag)
            compile_argv.append("-lelf")
            print(f"compile argv: {compile_argv!r}")
            compiled = run(compile_argv, cwd=work)
            sys.stdout.write(compiled.stdout)
            sys.stderr.write(compiled.stderr)
            if compiled.returncode != 0:
                return compiled.returncode

            runner = shlex.split(args.runner) if args.runner else []
            if args.runner and not runner:
                raise ValueError("--runner must contain an executable")
            run_argv = runner + [str(executable)]
            print(f"run argv: {run_argv!r}")
            result = run(run_argv, cwd=work)
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            if result.returncode != 0:
                return result.returncode
    except (OSError, ValueError) as error:
        print(f"run-libelf-abi: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
