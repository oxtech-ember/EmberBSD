#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Check that Clang compiles the OpenPAM module loader with -Werror.

dlfunc() returns dlfunc_t.  A direct cast to pam_func_t is a
-Wcast-function-type-mismatch error with Clang -Wextra -Werror.
"""

import argparse
from pathlib import Path
import subprocess
import sys
import unittest

SOURCE = Path(__file__).resolve().parents[2]
LOADER = SOURCE / "external/bsd/openpam/dist/lib/libpam/openpam_dynamic.c"
CLANG = None
SYSROOT = None


class OpenpamDynamicTests(unittest.TestCase):
    def test_clang_compiles_the_module_loader(self):
        # The warning options of the lib/libpam/libpam build.
        result = subprocess.run(
            [str(CLANG), "--target=aarch64--netbsd", "--sysroot=" + str(SYSROOT),
             "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wcast-qual",
             "-Wconversion", "-Werror", "-Wno-error=tautological-pointer-compare",
             "-fsyntax-only", "-DHAVE_CONFIG_H", "-DLIB_MAJ=4",
             "-I" + str(SOURCE / "lib/libpam/libpam"),
             "-I" + str(SOURCE / "external/bsd/openpam/dist/include"),
             "-DOPENPAM_MODULES_DIRECTORY=\"/usr/lib/security\"", str(LOADER)],
            text=True, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)


def main():
    global CLANG, SYSROOT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", type=Path, required=True)
    parser.add_argument("--sysroot", type=Path, required=True,
                        help="AArch64 sysroot with the libc headers")
    args, rest = parser.parse_known_args()
    if not args.clang.is_file() or not args.sysroot.is_dir():
        parser.error("supply an existing Clang and sysroot")
    CLANG, SYSROOT = args.clang, args.sysroot
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
