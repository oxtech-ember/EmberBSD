#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Exercise bsd.lib.mk export checks with a real versioned ELF library."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("make", "cc", "linker", "nm", "readelf", "shell", "native-tools", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--mk-root", type=Path,
                        help="optional make include overlay for a baseline check")
    args = parser.parse_args()
    for name in ("make", "cc", "linker", "nm", "readelf", "shell"):
        path = getattr(args, name)
        if not path.is_absolute() or not path.is_file():
            parser.error("--" + name + " must name an absolute tool")
    if not args.native_tools.is_absolute() or not args.native_tools.is_dir():
        parser.error("--native-tools must name an absolute utility directory")
    args.work.mkdir(parents=True, exist_ok=False)
    work = args.work.resolve()
    source = Path(__file__).resolve().parents[2]
    env = dict(os.environ, PATH=str(args.native_tools), LC_ALL="C")
    commands = []

    def run(argv, cwd=work, *, success=True):
        result = subprocess.run([str(v) for v in argv], cwd=cwd, env=env,
                                capture_output=True, text=True, timeout=30)
        commands.append(dict(argv=[str(v) for v in argv], cwd=str(cwd),
                             status=result.returncode,
                             output=result.stdout + result.stderr))
        (work / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
        if (result.returncode == 0) != success:
            raise RuntimeError(result.stdout + result.stderr)
        return result

    (work / "fixture.c").write_text(
        'int old_api(void) { return 1; }\n'
        'int new_api(void) { return 2; }\n'
        'int plain(void) { return 3; }\n'
        '__asm__(".symver old_api,api@VER_1");\n'
        '__asm__(".symver new_api,api@@VER_2");\n')
    (work / "fixture.map").write_text(
        'VER_1 { };\nVER_2 { global: api; plain; local: *; } VER_1;\n')
    run([args.cc, "--no-default-config", "--target=x86_64--netbsd", "-fPIC",
         "-c", "fixture.c", "-o", "fixture.o"])
    run([args.linker, "-shared", "--version-script=fixture.map", "fixture.o",
         "-o", "libfixture.so.1.0"])
    expected = "VER_1\nVER_2\napi@@VER_2\napi@VER_1\nplain@@VER_2\n"
    make = [args.make, "-r"]
    make += ["-m", source / "share/mk"]
    if args.mk_root:
        make += ["-m", args.mk_root]
    make += ["MAKECONF=/dev/null", "_SRC_TOP_=",
             "NETBSDSRCDIR=" + str(source), "MACHINE=amd64", "MACHINE_ARCH=x86_64",
             "MACHINE_GNU_PLATFORM=x86_64--netbsd", "USETOOLS=no", "MKDEBUG=no",
             "MKPROFILE=no", "MKDEBUGLIB=no", "MKLINT=no", "SOLIB=", "DPADD=",
             "DPLIBC=", "SHLIB_LDSTARTFILE=", "SHLIB_LDENDFILE=",
             "HOST_SH=" + str(args.shell), "EMBER_NATIVE_TOOLS=" + str(args.native_tools),
             "READELF=" + str(args.readelf)]

    def case(name, symbols=expected):
        directory = work / name
        directory.mkdir()
        shutil.copyfile(work / "libfixture.so.1.0", directory / "libfixture.so.1.0")
        (directory / "fixture.expsym").write_text(symbols)
        (directory / "Makefile").write_text(
            '.include <sys.mk>\nLIB=fixture\nSHLIB_MAJOR=1\nSHLIB_MINOR=0\n'
            'LIB_EXPSYM=fixture.expsym\n.include <bsd.lib.mk>\n')
        return directory

    valid = case("versioned")
    run(make + ["NM=" + str(args.nm), "libfixture.so.1.0.diffsym"], valid)
    assert (valid / "libfixture.so.1.0.actsym").read_text() == expected
    assert (valid / "libfixture.so.1.0.diffsym").read_bytes() == b""
    for suffix in ("raw", "names", "versions", "filtered", "tmp"):
        assert not (valid / ("libfixture.so.1.0.actsym." + suffix)).exists()
    print("versioned exported symbols: PASS", flush=True)

    mismatch = case("mismatch", expected.replace("api@@VER_2\n", ""))
    result = run(make + ["NM=" + str(args.nm), "libfixture.so.1.0.diffsym"],
                 mismatch, success=False)
    assert "actual symbols differ from expected symbols" in result.stdout + result.stderr
    assert not (mismatch / "libfixture.so.1.0.diffsym").exists()
    print("ABI mismatch rejected: PASS", flush=True)

    # A version name in the expected list must come from a real definition.
    (work / "plain.c").write_text('int plain(void) { return 3; }\n')
    run([args.cc, "--no-default-config", "--target=x86_64--netbsd", "-fPIC",
         "-c", "plain.c", "-o", "plain.o"])
    run([args.linker, "-shared", "plain.o", "-o", "unversioned.so"])
    missing = case("missing-version", "VER_1\nplain\n")
    shutil.copyfile(work / "unversioned.so", missing / "libfixture.so.1.0")
    result = run(make + ["NM=" + str(args.nm), "libfixture.so.1.0.diffsym"],
                 missing, success=False)
    assert "actual symbols differ from expected symbols" in result.stdout + result.stderr
    print("missing version definition rejected: PASS", flush=True)

    failing_nm = work / "failing-nm"
    failing_nm.write_text("#!" + str(args.shell) + "\n"
                          "printf '0000000000000000 T partial_export\\n'\nexit 7\n")
    failing_nm.chmod(0o755)
    for old in (False, True):
        directory = case("failed-nm-" + str(old))
        target = directory / "libfixture.so.1.0.actsym"
        if old:
            target.write_text("previous_valid_list\n")
            os.utime(target, (1, 1))
        run(make + ["NM=" + str(failing_nm), "libfixture.so.1.0.actsym"],
            directory, success=False)
        if old:
            assert target.read_text() == "previous_valid_list\n"
        else:
            assert not target.exists()
        for suffix in ("raw", "names", "versions", "filtered", "tmp"):
            assert not Path(str(target) + "." + suffix).exists()
    print("failed nm cannot publish or replace a symbol list: PASS", flush=True)

    failing_readelf = work / "failing-readelf"
    failing_readelf.write_text("#!" + str(args.shell) + "\n"
                              "printf 'Version definition section partial\\n'\nexit 7\n")
    failing_readelf.chmod(0o755)
    directory = case("failed-readelf")
    target = directory / "libfixture.so.1.0.actsym"
    target.write_text("previous_valid_list\n")
    os.utime(target, (1, 1))
    run(make + ["NM=" + str(args.nm), "READELF=" + str(failing_readelf),
                "libfixture.so.1.0.actsym"], directory, success=False)
    assert target.read_text() == "previous_valid_list\n"
    for suffix in ("raw", "names", "versions", "filtered", "tmp"):
        assert not Path(str(target) + "." + suffix).exists()
    print("failed readelf preserves the symbol list: PASS", flush=True)


if __name__ == "__main__":
    main()
