#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Check that the Clang profile never runs an external or host assembler.

The Clang profile has no GNU as. A -fno-integrated-as compile used to run the
first "as" in PATH, on Darwin the host's assembler, and pixman's AArch64 NEON
sources failed that way. Evaluates the real share/mk with the selected
make and LLVM prefix, then assembles the pixman sources.
"""

import argparse
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
STUB = SOURCE / "ember/tools/no-external-as"
PIXMAN = SOURCE / "external/mit/xorg/lib/pixman"
ARGS = None


def make_values(directory, *names):
    command = [str(ARGS.make), "-m", str(SOURCE / "share/mk"), "-C", str(directory),
               "MACHINE=evbarm", "MACHINE_ARCH=aarch64", "USETOOLS=yes",
               "TOOLCHAIN_MISSING=yes", "MKBINUTILS=no", "EMBER_COMPILER_FAMILY=clang",
               "EXTERNAL_TOOLCHAIN=" + str(ARGS.toolchain), "TOOLDIR=" + str(ARGS.tooldir),
               "NETBSDSRCDIR=" + str(SOURCE), "DESTDIR=" + str(ARGS.sysroot), "MKX11=yes", "X11SRCDIR=" + str(ARGS.x11_source)]
    for name in names:
        command += ["-V", "${%s}" % name]
    env = {"PATH": "/usr/bin:/bin", "HOME": tempfile.gettempdir()}
    result = subprocess.run(command, text=True, capture_output=True, env=env, timeout=60)
    if result.returncode:
        raise AssertionError(result.stderr)
    return [shlex.split(line) for line in result.stdout.splitlines()]


def assembler_jobs(cc, work, source):
    # -### prints the driver's jobs without running them.
    result = subprocess.run(cc + ["-fno-integrated-as", "-c", str(source), "-o",
                                  str(Path(work) / "out.o"), "-###"],
                            text=True, capture_output=True, timeout=60)
    jobs = [shlex.split(line) for line in result.stderr.splitlines() if line.startswith(' "')]
    return [job for job in jobs if "-cc1" not in job]


class NoExternalAssemblerTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.probe = Path(self.work.name) / "probe.s"
        self.probe.write_text("\t.text\n\t.globl probe\nprobe:\n\tret\n")

    def test_profile_compilers_resolve_the_failing_stub(self):
        for name in ("CC", "CXX", "AS"):
            cc, = make_values(PIXMAN, name)
            if name == "AS":
                cc = [word for word in cc if word not in ("-c", "-x", "assembler")]
            with self.subTest(tool=name):
                jobs = assembler_jobs(cc, self.work.name, self.probe)
                self.assertEqual(len(jobs), 1, jobs)
                self.assertEqual(Path(jobs[0][0]).resolve(), (STUB / "as").resolve(),
                                 "Clang would run an external assembler: " + jobs[0][0])

    def test_external_assembler_request_fails(self):
        cc, = make_values(PIXMAN, "CC")
        output = Path(self.work.name) / "probe.o"
        result = subprocess.run(cc + ["-fno-integrated-as", "-c", str(self.probe), "-o", str(output)],
                                text=True, capture_output=True, timeout=60)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("external assembler disabled", result.stderr)
        self.assertFalse(output.exists())

    def test_pixman_aarch64_neon_uses_the_integrated_assembler(self):
        cc, aflags, cppflags = make_values(PIXMAN, "CC", "AFLAGS", "CPPFLAGS")
        self.assertNotIn("-fno-integrated-as", aflags + cppflags)
        self.assertNotIn("-no-integrated-as", aflags + cppflags)
        dist = ARGS.x11_source / "external/mit/pixman/dist/pixman"
        for name in ("pixman-arma64-neon-asm.S", "pixman-arma64-neon-asm-bilinear.S"):
            with self.subTest(source=name):
                output = Path(self.work.name) / (name + ".o")
                result = subprocess.run(
                    cc + ["-fPIC", "-Werror"] + aflags + cppflags +
                    ["-x", "assembler-with-cpp", "-c", str(dist / name), "-o", str(output)],
                    text=True, capture_output=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stderr[-4000:])
                symbols = subprocess.run([str(ARGS.toolchain / "bin/llvm-nm"), "--defined-only",
                                          str(output)], text=True, capture_output=True, check=True)
                self.assertIn(" T pixman_", symbols.stdout)


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", required=True, type=Path, help="host bmake")
    parser.add_argument("--toolchain", required=True, type=Path, help="LLVM prefix (EXTERNAL_TOOLCHAIN)")
    parser.add_argument("--tooldir", required=True, type=Path)
    parser.add_argument("--x11-source", required=True, type=Path, help="prepared xsrc tree")
    parser.add_argument("--sysroot", required=True, type=Path, help="AArch64 base destination")
    ARGS, rest = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
