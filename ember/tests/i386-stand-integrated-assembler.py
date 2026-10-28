#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Check that the x86 boot programs assemble with Clang's integrated assembler.

The boot assembler sources used to add -no-integrated-as for Clang. The Clang
profile has no GNU as, so Clang ran the first "as" in PATH, the host's
assembler on Darwin. The profile now resolves that request to a stub that
fails. Evaluates the real makefiles for amd64 with the selected make and LLVM
prefix, then builds each boot assembler object in a temporary object tree.
"""

import argparse
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

TESTS = Path(__file__).resolve().parent
ARGS = None

# Directory below sys/arch/i386/stand, its assembler sources, and the ELF
# class each object must have.
OBJECTS = (
    ("mbr/mbr", ("mbr.S",), "ELF32"),
    ("mbr/mbr_com0_9600", ("mbr.S",), "ELF32"),
    ("mbr/gptmbr", ("gptmbr.S",), "ELF32"),
    ("fatboot/fat12", ("fatboot.S",), "ELF32"),
    ("cdboot", ("cdboot.S",), "ELF32"),
    ("bootxx/bootxx_ffsv1", ("bootxx.S", "pbr.S"), "ELF32"),
    ("boot/biosboot", ("biosboot.S",), "ELF32"),
    ("efiboot/bootx64", ("start.S",), "ELF64"),
    ("efiboot/bootia32", ("start.S",), "ELF32"),
)


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


class IntegratedAssemblerTests(unittest.TestCase):
    def setUp(self):
        work = tempfile.TemporaryDirectory()
        self.addCleanup(work.cleanup)
        self.objects = Path(work.name)

    def test_boot_sources_request_no_external_assembler(self):
        for directory, sources, _ in OBJECTS + (("lib", ("realprot.S",), None),):
            names = ["AFLAGS", "CPPFLAGS"] + ["AFLAGS." + source for source in sources]
            words = sum(make_values(directory, self.objects, *names), [])
            with self.subTest(directory=directory):
                self.assertNotIn("-no-integrated-as", words)
                self.assertNotIn("-fno-integrated-as", words)

    def test_boot_objects_build_with_the_integrated_assembler(self):
        readelf = ARGS.toolchain / "bin/llvm-readelf"
        for directory, sources, elf_class in OBJECTS:
            with self.subTest(directory=directory):
                command = make_command(directory, self.objects)
                obj = subprocess.run(command + ["obj"], text=True, capture_output=True,
                                     env=environment(self.objects), timeout=120)
                self.assertEqual(obj.returncode, 0, obj.stderr[-2000:])
                targets = [Path(source).stem + ".o" for source in sources]
                # The dependency pass creates the machine links; then build
                # only the assembler objects.
                for goals in (["depend"], targets):
                    built = subprocess.run(command + ["-j1"] + goals, text=True,
                                           capture_output=True, env=environment(self.objects),
                                           timeout=600)
                    output = built.stdout + built.stderr
                    self.assertNotIn("external assembler disabled", output)
                    self.assertEqual(built.returncode, 0, output[-4000:])
                objdir = self.objects / str(stand() / directory).lstrip("/")
                for target in targets:
                    header = subprocess.run([str(readelf), "-h", str(objdir / target)],
                                            text=True, capture_output=True, check=True)
                    self.assertIn("Class:                             " + elf_class, header.stdout)
                    self.assertIn("Machine:", header.stdout)


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
