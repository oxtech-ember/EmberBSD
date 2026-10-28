#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Check that the x86 EFI boot loaders' C sources build with Clang warnings as errors.

efipxe.c counted its network interfaces in a file-scope variable that nothing
read. Clang reports that with -Wunused-but-set-global, and the boot programs
build with -Werror, so bootx64.efi and bootia32.efi stopped at efipxe.o.
Builds efipxe.o for both loaders from the real makefiles in a temporary object
tree with the selected make and LLVM prefix.
"""

import argparse
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

TESTS = Path(__file__).resolve().parent
ARGS = None

LOADERS = ("efiboot/bootx64", "efiboot/bootia32")
def stand():
    return ARGS.source / "sys/arch/i386/stand"


def make_command(directory, objects):
    return [str(ARGS.make), "-m", str(ARGS.source / "share/mk"), "-C", str(stand() / directory),
            "MACHINE=amd64", "MACHINE_ARCH=x86_64", "USETOOLS=yes", "TOOLCHAIN_MISSING=yes",
            "MKBINUTILS=no", "EMBER_COMPILER_FAMILY=clang",
            "EXTERNAL_TOOLCHAIN=" + str(ARGS.toolchain), "TOOLDIR=" + str(ARGS.tooldir),
            "NETBSDSRCDIR=" + str(ARGS.source), "EMBER_LLVM_SOURCE=" + str(ARGS.llvm_source), "DESTDIR=" + str(objects / "destdir"),
            "MKREPRO=no"]


def environment(objects):
    return {"PATH": "/usr/bin:/bin", "HOME": tempfile.gettempdir(),
            "MAKEOBJDIRPREFIX": str(objects)}


def make_values(directory, objects, *names):
    command = make_command(directory, objects)
    for name in names:
        command += ["-V", "${%s}" % name]
    result = subprocess.run(command, text=True, capture_output=True,
                            env=environment(objects), timeout=60)
    if result.returncode:
        raise AssertionError(result.stderr)
    return [shlex.split(line) for line in result.stdout.splitlines()]


class EfiPxeWarningTests(unittest.TestCase):
    def setUp(self):
        work = tempfile.TemporaryDirectory()
        self.addCleanup(work.cleanup)
        self.objects = Path(work.name)

    def test_loaders_build_with_warnings_as_errors(self):
        for directory in LOADERS:
            words = sum(make_values(directory, self.objects, "CFLAGS"), [])
            with self.subTest(directory=directory):
                self.assertIn("-Werror", words)

    def test_efipxe_builds(self):
        for directory in LOADERS:
            with self.subTest(directory=directory):
                command = make_command(directory, self.objects)
                for goals in (["obj"], ["depend"], ["efipxe.o"]):
                    built = subprocess.run(command + ["-j1"] + goals, text=True,
                                           capture_output=True, env=environment(self.objects),
                                           timeout=600)
                    output = built.stdout + built.stderr
                    self.assertNotIn("-Wunused-but-set-global", output)
                    self.assertEqual(built.returncode, 0, output[-4000:])
                objdir = self.objects / str(stand() / directory).lstrip("/")
                self.assertTrue((objdir / "efipxe.o").is_file())


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", required=True, type=Path, help="host bmake")
    parser.add_argument("--toolchain", required=True, type=Path, help="LLVM prefix (EXTERNAL_TOOLCHAIN)")
    parser.add_argument("--tooldir", required=True, type=Path, help="OS host tools")
    parser.add_argument("--llvm-source", required=True, type=Path,
                        help="Ports-prepared LLVM source root (EMBER_LLVM_SOURCE) for libkern")
    parser.add_argument("--source", type=Path, default=TESTS.parents[1],
                        help="OS source tree (default: the tree holding this test)")
    ARGS, rest = parser.parse_known_args()
    ARGS.source = ARGS.source.resolve()
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
