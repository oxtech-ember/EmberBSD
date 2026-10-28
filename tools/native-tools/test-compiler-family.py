#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Evaluate real OS compiler selection with an explicit NetBSD make."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
MAKE = None


class CompilerFamilyTests(unittest.TestCase):
    def query(self, names, *variables, host_include=None):
        with tempfile.TemporaryDirectory(prefix="ember-compiler-family-") as work:
            fixture = Path(work) / "probe.mk"
            fixture.write_text('.include <sys.mk>\n.include <bsd.own.mk>\n'
                               '.include <bsd.sys.mk>\n' +
                               ('.include <' + host_include + '>\n' if host_include else ''))
            command = [str(MAKE), "-r", "-m", str(SOURCE / "share/mk"),
                "-f", str(fixture), "MAKECONF=/dev/null", "_SRC_TOP_=",
                "USETOOLS=yes", "MACHINE=evbarm", "MACHINE_ARCH=aarch64",
                "MACHINE_GNU_PLATFORM=aarch64--netbsd", "TOOLDIR=/build/tools",
                "DESTDIR=/target/root", "HOST_OSTYPE=Darwin-27-arm64",
                "EXTERNAL_TOOLCHAIN=/sdk", *variables]
            for name in names:
                command.extend(("-V", "${" + name + "}"))
            return subprocess.run(command, cwd=work, text=True,
                capture_output=True, timeout=30)

    def values(self, names, *variables, host_include=None):
        result = self.query(names, *variables, host_include=host_include)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        values = result.stdout.splitlines()
        self.assertEqual(len(values), len(names), result.stdout + result.stderr)
        return dict(zip(names, values))

    def test_default_uses_llvm_and_an_explicit_target(self):
        names = ("CC", "CPP", "CXX", "AS", "LD", "AR", "RANLIB", "NM",
                 "OBJCOPY", "OBJDUMP", "READELF", "SIZE", "STRINGS", "STRIP",
                 "ACTIVE_CC", "ACTIVE_CXX", "LDFLAGS", "LD_DRIVER_FLAGS")
        value = self.values(names)
        self.assertEqual(value["CC"], "/sdk/bin/clang --target=aarch64--netbsd")
        self.assertEqual(value["CPP"], "/sdk/bin/clang --driver-mode=cpp --target=aarch64--netbsd")
        self.assertEqual(value["CXX"], "/sdk/bin/clang++ --target=aarch64--netbsd")
        self.assertEqual(value["AS"], "/sdk/bin/clang --target=aarch64--netbsd -c -x assembler")
        self.assertEqual(value["LD"], "/sdk/bin/ld.lld")
        for name in ("AR", "RANLIB", "NM", "OBJCOPY", "OBJDUMP", "READELF", "SIZE", "STRINGS", "STRIP"):
            self.assertEqual(value[name], "/sdk/bin/llvm-" + name.lower())
        self.assertEqual(value["ACTIVE_CC"].strip(), "clang")
        self.assertEqual(value["ACTIVE_CXX"].strip(), "clang")
        self.assertNotIn("--ld-path=/sdk/bin/ld.lld", value["LDFLAGS"].split())
        self.assertIn("--ld-path=/sdk/bin/ld.lld", value["LD_DRIVER_FLAGS"].split())

    def test_linker_selection_is_only_in_driver_link_rules(self):
        compile_rules = ("COMPILE.c", "COMPILE.cc", "COMPILE.m", "COMPILE.s", "COMPILE.S")
        link_rules = ("LINK.c", "LINK.cc", "LINK.m", "LINK.s", "LINK.S")
        values = self.values((*compile_rules, *link_rules, "LDFLAGS", "LD"))
        flag = "--ld-path=/sdk/bin/ld.lld"
        for name in compile_rules:
            with self.subTest(rule=name):
                self.assertNotIn(flag, values[name].split())
        for name in link_rules:
            with self.subTest(rule=name):
                self.assertIn(flag, values[name].split())
        self.assertNotIn(flag, values["LDFLAGS"].split())
        self.assertEqual(values["LD"], "/sdk/bin/ld.lld")

    def test_dependency_compilers_keep_target_arguments_outside_cc(self):
        names = ('MKDEP', 'MKDEPCXX', 'MKDEPOBJC', '_MKDEP_TARGET_FLAGS')
        value = self.values(names)
        for name, compiler in (('MKDEP', 'clang'), ('MKDEPCXX', 'clang++'),
                               ('MKDEPOBJC', 'clang')):
            self.assertEqual(value[name], 'CC=/sdk/bin/' + compiler + ' /build/tools/bin/nbmkdep')
        self.assertIn('--target=aarch64--netbsd', value['_MKDEP_TARGET_FLAGS'].split())
        value = self.values(names, 'EMBER_COMPILER_FAMILY=gcc')
        self.assertEqual(value['MKDEP'], 'CC=/sdk/bin/aarch64--netbsd-gcc /build/tools/bin/nbmkdep')
        self.assertNotIn('--target=', value['_MKDEP_TARGET_FLAGS'])

    def test_host_dependencies_do_not_inherit_target_flags(self):
        for include in ('bsd.hostprog.mk', 'bsd.hostlib.mk'):
            value = self.values(('_MKDEP_TARGET_FLAGS', 'MKDEPFLAGS'),
                'MKDEPFLAGS=-Dcaller_owned', host_include=include)
            self.assertEqual(value['_MKDEP_TARGET_FLAGS'].strip(), '')
            self.assertEqual(value['MKDEPFLAGS'], '-Dcaller_owned')

    def test_explicit_gcc_retains_its_prefixed_tools(self):
        names = ("CC", "CPP", "CXX", "AS", "LD", "AR", "ACTIVE_CC", "LD_DRIVER_FLAGS", "LDFLAGS")
        value = self.values(names, "EMBER_COMPILER_FAMILY=gcc")
        for name, suffix in (("CC", "gcc"), ("CPP", "cpp"), ("CXX", "c++"),
                             ("AS", "as"), ("LD", "ld"), ("AR", "ar")):
            self.assertEqual(value[name], "/sdk/bin/aarch64--netbsd-" + suffix)
        self.assertEqual(value["ACTIVE_CC"].strip(), "gcc")
        self.assertNotIn("--ld-path=", value["LDFLAGS"])
        self.assertEqual(value["LD_DRIVER_FLAGS"].strip(), "")

    def test_unknown_family_and_unsafe_prefix_fail(self):
        for setting in ("EMBER_COMPILER_FAMILY=unknown",
                        "EMBER_HOST_COMPILER_FAMILY=unknown",
                        "AVAILABLE_COMPILER=gcc clang false",
                        "AVAILABLE_COMPILER=clang gcc false",
                        "EXTERNAL_TOOLCHAIN=relative", "EXTERNAL_TOOLCHAIN=/sdk path",
                        "EXTERNAL_TOOLCHAIN=/sdk ",
                        "EXTERNAL_TOOLCHAIN=/sdk;bad"):
            with self.subTest(setting=setting):
                result = self.query(("CC",), setting)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_gcc_runtime_cannot_select_gcc_implicitly(self):
        result = self.query(("CC",), "EMBER_EXTERNAL_GCC_RUNTIME=/runtime")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("EMBER_EXTERNAL_GCC_RUNTIME", result.stderr)

    def test_legacy_feature_switches_cannot_change_the_selected_family(self):
        for settings in (("HAVE_LLVM=no",), ("HAVE_GCC=16",),
                         ("EMBER_COMPILER_FAMILY=gcc", "HAVE_LLVM=yes")):
            with self.subTest(settings=settings):
                result = self.query(("CC", "HAVE_LIBGCC", "HAVE_LIBGCC_EH"),
                                    *settings)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("EMBER_COMPILER_FAMILY", result.stderr)

    def test_reproducible_maps_do_not_pass_gcc_options_to_clang(self):
        for family in ("clang", "gcc"):
            with self.subTest(family=family):
                value = self.values(("CPPFLAGS", "CFLAGS", "CXXFLAGS"),
                    "EMBER_COMPILER_FAMILY=" + family, "MKREPRO=yes",
                    "NETBSDSRCDIR=/source", "NETBSDOBJDIR=/objects",
                    "X11SRCDIR=/xsource")
                self.assertEqual("-Wp,-fno-canonical-system-headers" in
                                 value["CPPFLAGS"].split(), family == "gcc")
                self.assertNotIn("-iremap", value["CPPFLAGS"])
                for name in ("CFLAGS", "CXXFLAGS"):
                    for source, target in (("/source", "/usr/src"),
                                           ("/objects", "/usr/obj"),
                                           ("/target/root", ""),
                                           ("/xsource", "/usr/xsrc")):
                        for kind in ("debug", "macro"):
                            self.assertIn("-f" + kind + "-prefix-map=" + source +
                                          "=" + target, value[name].split())

    def test_dtrace_flags_follow_independent_host_and_target_families(self):
        names = ("DTRACE_OPTS", "HOST_DTRACE_OPTS")
        for target, host in (("clang", "clang"), ("clang", "gcc"),
                             ("gcc", "clang"), ("gcc", "gcc")):
            with self.subTest(target=target, host=host):
                value = self.values(names, "MKDTRACE=yes",
                    "EMBER_COMPILER_FAMILY=" + target,
                    "EMBER_HOST_COMPILER_FAMILY=" + host)
                self.assertEqual("-fno-ipa-sra" in value["DTRACE_OPTS"].split(), target == "gcc")
                self.assertEqual("-fno-ipa-icf" in value["DTRACE_OPTS"].split(), target == "gcc")
                self.assertEqual("-fno-ipa-sra" in value["HOST_DTRACE_OPTS"].split(), host == "gcc")
                for name in names:
                    self.assertIn("-fno-omit-frame-pointer", value[name].split())
                    self.assertIn("-fno-optimize-sibling-calls", value[name].split())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", required=True, type=Path,
        help="Explicit NetBSD make used for metadata checks, not a compiler build.")
    parser.add_argument("--source", type=Path, default=SOURCE,
        help="OS source under test; select an earlier export for a negative control.")
    args, remaining = parser.parse_known_args()
    MAKE = args.make.absolute()
    SOURCE = args.source.absolute()
    if not MAKE.is_file() or not os.access(MAKE, os.X_OK):
        parser.error("--make must name an executable file")
    unittest.main(argv=[sys.argv[0], *remaining])
