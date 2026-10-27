#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Check that fixed sources keep their diagnostics under Clang and GCC.

dt_link.c, dt_pid.c and openpam_dynamic.c were fixed in the code, so their
per-file COPTS must not demote a warning with -Wno-error= for any compiler,
and must not give Clang an option that disables the fixed warning.
GCC-only options that bsd.own.mk or an ${ACTIVE_CC} test select stay allowed.
libpam no longer has sources that trip Clang's tautological-pointer-compare,
so its library-wide CWARNFLAGS/COPTS must not demote or disable it either.
The COPTS/CWARNFLAGS lines and the CC_WNO_* definitions are evaluated with bmake.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
OWN_MK = SOURCE / "share/mk/bsd.own.mk"
# (Makefile, source, warnings fixed in that source)
CASES = (
    ("external/cddl/osnet/lib/libdtrace/Makefile", "dt_link.c",
     ("unused-but-set-variable",)),
    ("external/cddl/osnet/lib/libdtrace/Makefile", "dt_pid.c",
     ("unused-but-set-variable",)),
    ("lib/libpam/libpam/Makefile", "openpam_dynamic.c",
     ("cast-function-type", "cast-function-type-mismatch")),
)
# (Makefile, warnings fixed in every source of that library)
LIBRARY_CASES = (
    ("lib/libpam/libpam/Makefile", ("tautological-pointer-compare",)),
)
# Library-wide options; bsd.sys.mk adds CWARNFLAGS.${ACTIVE_CC} to CWARNFLAGS.
LIBRARY_VARIABLES = ("COPTS", "CWARNFLAGS", "CWARNFLAGS.clang")
COMPILERS = (("clang", None), ("gcc", "16"))
MAKE = None


def copts(makefile, source, compiler, gcc_version):
    """Return the COPTS.<source> options for a compiler, evaluated by bmake."""
    return evaluate(makefile, ("COPTS." + source,), compiler, gcc_version)


def library_options(makefile, compiler, gcc_version):
    """Return the library-wide warning options for a compiler."""
    return evaluate(makefile, LIBRARY_VARIABLES, compiler, gcc_version,
                    "${COPTS} ${CWARNFLAGS} ${CWARNFLAGS.${ACTIVE_CC}}")


def evaluate(makefile, variables, compiler, gcc_version, expression=None):
    """Evaluate the assignments to variables in makefile with bmake."""
    if expression is None:
        expression = " ".join("${%s}" % v for v in variables)
    pattern = re.compile(r"(?:%s)\s*[+:?!]?=" %
                         "|".join(re.escape(v) for v in variables))
    lines = [line for line in (SOURCE / makefile).read_text().splitlines()
             if pattern.match(line)]
    wno = [line for line in OWN_MK.read_text().splitlines()
           if re.match(r"CC_WNO_\w+\s*=", line)]
    probe = "ACTIVE_CC=%s\n" % compiler
    if gcc_version is not None:
        probe += "HAVE_GCC=%s\n" % gcc_version
    probe += "\n".join(wno + lines) + "\n"
    with tempfile.TemporaryDirectory() as work:
        (Path(work) / "probe.mk").write_text(probe)
        result = subprocess.run(
            [str(MAKE), "-r", "-f", "probe.mk", "-V", expression],
            cwd=work, text=True, capture_output=True, timeout=30)
    if result.returncode:
        raise AssertionError(result.stderr)
    return result.stdout.split()


class WarningDemoteTests(unittest.TestCase):
    def test_own_mk_defines_the_gcc_only_options(self):
        text = OWN_MK.read_text()
        self.assertRegex(text, r"(?m)^CC_WNO_CAST_FUNCTION_TYPE\s*=")

    def test_no_compiler_gets_a_demoted_error(self):
        for makefile, source, _ in CASES:
            for compiler, version in COMPILERS:
                with self.subTest(source=source, compiler=compiler):
                    demotes = [o for o in copts(makefile, source, compiler, version)
                               if o.startswith("-Wno-error")]
                    self.assertEqual(demotes, [], "%s: %s" % (makefile, source))

    def test_clang_keeps_the_fixed_warnings(self):
        for makefile, source, warnings in CASES:
            with self.subTest(source=source):
                options = copts(makefile, source, "clang", None)
                disabled = [o for o in options for w in warnings
                            if o in ("-Wno-" + w, "-Wno-error=" + w)]
                self.assertEqual(disabled, [], "%s: %s" % (makefile, source))

    def test_clang_keeps_library_wide_fixed_warnings(self):
        for makefile, warnings in LIBRARY_CASES:
            with self.subTest(makefile=makefile):
                options = library_options(makefile, "clang", None)
                disabled = [o for o in options for w in warnings
                            if o in ("-Wno-" + w, "-Wno-error=" + w)]
                self.assertEqual(disabled, [], makefile)


def main():
    global MAKE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", type=Path, required=True, help="bmake")
    args, rest = parser.parse_known_args()
    if not args.make.is_file():
        parser.error("%s is not a file" % args.make)
    MAKE = args.make
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
