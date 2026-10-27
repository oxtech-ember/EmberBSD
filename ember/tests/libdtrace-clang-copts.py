#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Check the libdtrace compiler options and compatibility macros with Clang.

Clang with -Werror rejects a warning option that only GCC knows, such as
-Wno-error=use-after-free, so GCC-only options must not reach Clang.
The libproc compatibility macros must evaluate the link map ID that the
caller sets, or Clang reports -Wunused-but-set-variable in dt_pid.c.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
MAKEFILE = SOURCE / "external/cddl/osnet/lib/libdtrace/Makefile"
COMPAT = SOURCE / "external/cddl/osnet/lib/libdtrace/libproc_compat.h"
# The dt_proc.c and dt_pid_get_types() patterns: LM_ID_BASE, and a link map
# ID that is set and then only given to Pxlookup_by_name().
LOOKUP_FIXTURE = """\
#include "libproc_compat.h"
int proc_name2sym(void *, const char *, const char *, void *, void *);
int lookup(void *p, const char *mod, const char *fn, void *sym, void *si, int named);
int
lookup(void *p, const char *mod, const char *fn, void *sym, void *si, int named)
{
	long lmid;

	if (Pxlookup_by_name(p, LM_ID_BASE, "a.out", "main", sym, si) == 0)
		return 0;
	if (named)
		lmid = 1;
	else
		lmid = 0;
	return Pxlookup_by_name(p, lmid, mod, fn, sym, si);
}
"""
MAKE = None
CLANG = None


def per_file_options(compiler, gcc_version):
    """Return {variable: options} for the COPTS.* lines with bmake."""
    lines = [line for line in MAKEFILE.read_text().splitlines()
             if re.match(r"COPTS\.\S+\s*\+?=", line)]
    names = sorted({re.match(r"(COPTS\.\S+?)\s*\+?=", line).group(1) for line in lines})
    probe = "ACTIVE_CC=%s\n" % compiler
    if gcc_version is not None:
        probe += "HAVE_GCC=%s\n" % gcc_version
    probe += "\n".join(lines) + "\n"
    with tempfile.TemporaryDirectory() as work:
        (Path(work) / "probe.mk").write_text(probe)
        command = [str(MAKE), "-r", "-f", "probe.mk"]
        for name in names:
            command += ["-V", "${%s}" % name]
        result = subprocess.run(command, cwd=work, text=True,
                                capture_output=True, timeout=30)
    if result.returncode:
        raise AssertionError(result.stderr)
    return dict(zip(names, result.stdout.splitlines()))


class LibdtraceCloptsTests(unittest.TestCase):
    def test_gcc_keeps_its_use_after_free_option(self):
        options = per_file_options("gcc", "16")
        self.assertIn("-Wno-error=use-after-free", options["COPTS.dt_open.c"].split())

    def test_clang_gets_no_use_after_free_option(self):
        options = per_file_options("clang", None)
        self.assertNotIn("use-after-free", options["COPTS.dt_open.c"])

    def test_clang_accepts_every_per_file_option(self):
        if CLANG is None:
            self.skipTest("pass --clang to give the options to Clang")
        for name, value in per_file_options("clang", None).items():
            with self.subTest(variable=name):
                result = subprocess.run(
                    [str(CLANG), "--target=aarch64--netbsd", "-Werror", "-fsyntax-only",
                     "-x", "c", "-"] + value.split(),
                    input="int dtrace_probe;\n", text=True, capture_output=True,
                    timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_lookup_by_name_uses_the_link_map_id(self):
        if CLANG is None:
            self.skipTest("pass --clang to compile the libproc_compat.h fixture")
        result = subprocess.run(
            [str(CLANG), "--target=aarch64--netbsd", "-Wall", "-Werror", "-fsyntax-only",
             "-I" + str(COMPAT.parent), "-x", "c", "-"],
            input=LOOKUP_FIXTURE, text=True, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stderr)


def main():
    global MAKE, CLANG
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", type=Path, required=True)
    parser.add_argument("--clang", type=Path)
    args, rest = parser.parse_known_args()
    for path in (args.make, args.clang):
        if path is not None and not path.is_file():
            parser.error("%s is not a file" % path)
    MAKE, CLANG = args.make, args.clang
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
