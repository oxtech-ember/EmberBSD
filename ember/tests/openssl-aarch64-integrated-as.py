#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Check that Clang builds AArch64 libcrypto with its integrated assembler.

With -no-integrated-as, Clang on a Darwin host runs Apple as, which rejects
the ELF assembly of every libcrypto C file.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
TREES = ("crypto/external/apache2/openssl", "crypto/external/bsd/openssl")
MAKE = None
CLANG = None
SYSROOT = None


class IntegratedAssemblerTests(unittest.TestCase):
    def test_clang_flags_keep_the_integrated_assembler(self):
        for tree in TREES:
            with self.subTest(tree=tree), tempfile.TemporaryDirectory() as work:
                arch = Path(work) / "arch/aarch64"
                arch.mkdir(parents=True)
                shutil.copyfile(SOURCE / tree / "lib/libcrypto/arch/aarch64/crypto.inc",
                                arch / "crypto.inc")
                # Stub for the common fragment that the AArch64 one includes.
                (Path(work) / "crypto.inc").write_text("")
                (Path(work) / "probe.mk").write_text(
                    '.SUFFIXES: .S\nACTIVE_CC=clang\nHAVE_LLVM=yes\n.include "arch/aarch64/crypto.inc"\n')
                result = subprocess.run(
                    [str(MAKE), "-r", "-f", "probe.mk", "-V", "${CFLAGS} ${CPPFLAGS}"],
                    cwd=work, text=True, capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                flags = result.stdout.split()
                self.assertIn("-D__ARM_MAX_ARCH__=8", flags)
                self.assertNotIn("-no-integrated-as", flags)
                self.assertNotIn("-fno-integrated-as", flags)

    def test_clang_assembles_the_aarch64_sources(self):
        if CLANG is None:
            self.skipTest("pass --clang and --sysroot to assemble the AArch64 sources")
        for tree in TREES:
            dist = SOURCE / tree / "dist"
            for source in sorted((SOURCE / tree / "lib/libcrypto/arch/aarch64").glob("*.S")):
                with self.subTest(source=str(source.relative_to(SOURCE))), \
                        tempfile.TemporaryDirectory() as work:
                    output = Path(work) / "out.o"
                    result = subprocess.run(
                        [str(CLANG), "--target=aarch64--netbsd", "--sysroot=" + str(SYSROOT),
                         "-fintegrated-as", "-Werror", "-fPIC", "-D__ARM_MAX_ARCH__=8",
                         "-I" + str(dist), "-I" + str(dist / "include"),
                         "-I" + str(dist / "crypto"), "-I" + str(dist / "crypto/include"),
                         "-c", str(source), "-o", str(output)],
                        text=True, capture_output=True, timeout=120)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    # ELF magic, ELFCLASS64, little endian, e_machine EM_AARCH64.
                    header = output.read_bytes()[:20]
                    self.assertEqual(header[:6], b"\x7fELF\x02\x01")
                    self.assertEqual(header[18:20], b"\xb7\x00")


def main():
    global MAKE, CLANG, SYSROOT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", type=Path, required=True)
    parser.add_argument("--clang", type=Path)
    parser.add_argument("--sysroot", type=Path)
    args, rest = parser.parse_known_args()
    if (args.clang is None) != (args.sysroot is None):
        parser.error("--clang and --sysroot go together")
    for path in (args.make, args.clang, args.sysroot):
        if path is not None and not path.is_absolute():
            parser.error("tool and sysroot paths must be absolute")
    MAKE, CLANG, SYSROOT = args.make, args.clang, args.sysroot
    unittest.main(argv=[sys.argv[0], *rest])


if __name__ == "__main__":
    main()
