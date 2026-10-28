#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Link the x86 BIOS boot programs with ld.lld and check their size limits.

The boot programs are fixed-address images. GNU ld links them statically from
their -Ttext address. ld.lld adds .interp unless it is told -static and puts
the first section at its 0x400000 image base, so the raw images no longer fit
their sectors; it also rejects the single-dash -cref option. Makefile.ld adds
-static, --image-base=0 and --cref only for ld.lld.

Builds every BIOS boot program (not efiboot) from the real makefiles for amd64
in a temporary object tree and checks: each sector image has its exact size
and no negative free space, each bootxx fits BOOTXX_SECTORS, dosboot.com fits
a COM file, and every linked ELF image has no PT_INTERP, starts at its load
address and keeps real_to_prot in the first 64 KiB. Also checks that only the
ld.lld profile gets the extra options.
"""

import argparse
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

TESTS = Path(__file__).resolve().parent
ARGS = None

PROGRAMS = "mbr boot bootxx cdboot fatboot dosboot pxeboot"

# Raw images with an exact size.
IMAGES = {
    "mbr/mbr/mbr": 512, "mbr/mbr_bootsel/mbr_bootsel": 512, "mbr/mbr_ext/mbr_ext": 512,
    "mbr/mbr_com0/mbr_com0": 512, "mbr/mbr_com0_9600/mbr_com0_9600": 512,
    "mbr/gptmbr/gptmbr.bin": 440,
    "fatboot/fat12/bootxx_fat12": 512, "fatboot/fat16/bootxx_fat16": 512,
    "cdboot/bootxx_cd9660": 2048,
}
BOOTXX = ("ext2fs", "ffsv1", "ffsv2", "lfsv1", "lfsv2", "msdos", "ustarfs")
# Linked images kept as ELF and the address their first segment must load at.
LINKED = {"boot/biosboot/boot.sym": 0, "dosboot/dosboot.sym": 0x100,
          "pxeboot/pxeboot_ia32.sym": 0}
LINKED.update({f"bootxx/bootxx_{fs}/bootxx_{fs}.sym": 0x1000 for fs in BOOTXX})
COM_LIMIT = 0x10000 - 0x100
PROFILE_DIRS = ("mbr/mbr", "mbr/gptmbr", "fatboot/fat12", "cdboot", "bootxx/bootxx_ffsv1",
                "boot/biosboot", "dosboot", "pxeboot")


def stand():
    return ARGS.source / "sys/arch/i386/stand"


def make_command(directory, objects, family="clang"):
    return [str(ARGS.make), "-m", str(ARGS.source / "share/mk"), "-C", str(stand() / directory),
            "MACHINE=amd64", "MACHINE_ARCH=x86_64", "USETOOLS=yes", "TOOLCHAIN_MISSING=yes",
            "MKBINUTILS=no", "EMBER_COMPILER_FAMILY=" + family,
            "EXTERNAL_TOOLCHAIN=" + str(ARGS.toolchain), "TOOLDIR=" + str(ARGS.tooldir),
            "NETBSDSRCDIR=" + str(ARGS.source), "EMBER_LLVM_SOURCE=" + str(ARGS.llvm_source),
            "DESTDIR=" + str(objects / "destdir"),
            "MKREPRO=no"]


def environment(objects):
    return {"PATH": "/usr/bin:/bin", "HOME": tempfile.gettempdir(),
            "MAKEOBJDIRPREFIX": str(objects)}


def make_values(directory, objects, *names, family="clang"):
    command = make_command(directory, objects, family)
    for name in names:
        command += ["-V", "${%s}" % name]
    result = subprocess.run(command, text=True, capture_output=True,
                            env=environment(objects), timeout=60)
    if result.returncode:
        raise AssertionError(result.stderr)
    return [shlex.split(line) for line in result.stdout.splitlines()]


class LldBootProgramTests(unittest.TestCase):
    def test_only_the_lld_profile_gets_the_lld_options(self):
        with tempfile.TemporaryDirectory() as work:
            objects = Path(work)
            for directory in PROFILE_DIRS:
                with self.subTest(directory=directory):
                    clang = make_values(directory, objects, "LD_DRIVER_FLAGS", "STAND_LD_CREF")
                    self.assertIn("-static", clang[0])
                    self.assertIn("-Wl,--image-base=0", clang[0])
                    self.assertEqual(clang[1], ["-Wl,--cref"])
                    gcc = make_values(directory, objects, "LD_DRIVER_FLAGS", "STAND_LD_CREF",
                                      family="gcc")
                    self.assertNotIn("-static", gcc[0])
                    self.assertNotIn("-Wl,--image-base=0", gcc[0])
                    self.assertEqual(gcc[1], ["-Wl,-cref"])


class LldBootProgramBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.objects = Path(cls.work.name)
        cls.output = ""
        cls.status = 0
        # Build each program directory on its own: a SUBDIR override on the
        # command line would reach every nested make as well.
        for directory in PROGRAMS.split():
            command = make_command(directory, cls.objects)
            for goals in (["obj"], ["-j4", "dependall"]):
                built = subprocess.run(command + goals, text=True, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, env=environment(cls.objects),
                                       timeout=3600)
                cls.output += built.stdout
                cls.status = cls.status or built.returncode
        cls.objdir = cls.objects / str(stand()).lstrip("/")

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def test_build_succeeds(self):
        self.assertEqual(self.status, 0, self.output[-6000:])
        self.assertNotIn("is larger than", self.output)

    def test_sector_images_have_their_size_and_free_space(self):
        free = dict((name, int(count)) for count, name in
                    re.findall(r"^#### There are (-?\d+) free bytes in (\S+)$", self.output, re.M))
        for image, size in IMAGES.items():
            with self.subTest(image=image):
                path = self.objdir / image
                self.assertTrue(path.is_file(), image)
                self.assertEqual(path.stat().st_size, size)
                name = Path(image).name
                if name in free:
                    self.assertGreaterEqual(free[name], 0)
        for name in ("mbr", "mbr_bootsel", "mbr_ext", "mbr_com0", "mbr_com0_9600",
                     "bootxx_fat12", "bootxx_fat16", "bootxx_cd9660"):
            self.assertIn(name, free)

    def test_bootxx_fits_its_sectors(self):
        for fs in BOOTXX:
            directory = f"bootxx/bootxx_{fs}"
            with self.subTest(directory=directory):
                sectors = int(make_values(directory, self.objects, "BOOTXX_SECTORS")[0][0])
                path = self.objdir / directory / f"bootxx_{fs}"
                self.assertTrue(path.is_file())
                self.assertLessEqual(path.stat().st_size, sectors * 512)
                self.assertEqual(path.stat().st_size % 512, 0)
                found = re.search(rf"^bootxx_{fs} size (\d+), (-?\d+) free$", self.output, re.M)
                self.assertIsNotNone(found)
                self.assertGreaterEqual(int(found.group(2)), 0)

    def test_dosboot_fits_a_com_file(self):
        self.assertLessEqual((self.objdir / "dosboot/dosboot.com").stat().st_size, COM_LIMIT)

    def test_linked_images_start_at_their_load_address(self):
        tools = ARGS.toolchain / "bin"
        for image, address in LINKED.items():
            with self.subTest(image=image):
                path = self.objdir / image
                headers = subprocess.run([str(tools / "llvm-readelf"), "-lW", str(path)],
                                         text=True, capture_output=True, check=True).stdout
                self.assertNotIn("INTERP", headers)
                loads = [int(line.split()[2], 16) for line in headers.splitlines()
                         if line.split()[:1] == ["LOAD"]]
                self.assertEqual(min(loads), address)
                symbols = subprocess.run([str(tools / "llvm-nm"), str(path)], text=True,
                                         capture_output=True, check=True).stdout
                values = {fields[2]: int(fields[0], 16) for fields in map(str.split, symbols.splitlines())
                          if len(fields) == 3}
                self.assertLess(values["real_to_prot"], 0x10000)
                if "start" in values:
                    self.assertEqual(values["start"], address)


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", required=True, type=Path, help="host bmake")
    parser.add_argument("--toolchain", required=True, type=Path, help="LLVM prefix (EXTERNAL_TOOLCHAIN)")
    parser.add_argument("--tooldir", required=True, type=Path, help="OS host tools")
    parser.add_argument("--llvm-source", required=True, type=Path,
                        help="Ports-prepared LLVM source root (EMBER_LLVM_SOURCE)")
    parser.add_argument("--source", type=Path, default=TESTS.parents[1],
                        help="OS source tree (default: the tree holding this test)")
    ARGS, rest = parser.parse_known_args()
    ARGS.source = ARGS.source.resolve()
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
