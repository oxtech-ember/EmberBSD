#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Check common LLVM source routing with an explicit NetBSD make."""

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
MAKE = None
PREPARED_LLVM = None


class LLVMSourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ember-llvm-source-")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.llvm = self.work / "llvm-source"
        # Source-route fixtures only; these are never compiled or packaged.
        for name in ("runtimes/CMakeLists.txt", "compiler-rt/lib/builtins/int_lib.h",
                     "compiler-rt/lib/builtins/comparetf2.c",
                     "compiler-rt/lib/builtins/int_util.c",
                     "compiler-rt/include/profile/InstrProfData.inc",
                     "libunwind/include/unwind.h",
                     "libunwind/include/__libunwind_config.h",
                     "libunwind/include/unwind_itanium.h",
                     "libunwind/include/unwind_arm_ehabi.h"):
            path = self.llvm / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("source route fixture\n")

    def query(self, consumer, names, *variables, fixture_extra=""):
        fixture = self.work / "probe.mk"
        fixture.write_text('.include <sys.mk>\n.include <bsd.own.mk>\n'
            'LIBC_MACHINE_CPU=aarch64\nLIBC_MACHINE_ARCH=aarch64\n'
            '.include "' + str(SOURCE / consumer) + '"\n' + fixture_extra)
        command = [str(MAKE), "-r", "-m", str(SOURCE / "share/mk"),
            "-f", str(fixture), "MAKECONF=/dev/null", "_SRC_TOP_=",
            "USETOOLS=yes", "MACHINE=evbarm", "MACHINE_ARCH=aarch64",
            "MKPROFILE=no", "MKDEBUGLIB=no",
            "MACHINE_GNU_PLATFORM=aarch64--netbsd", "TOOLDIR=/build/tools",
            "DESTDIR=/target/root", "NETBSDSRCDIR=" + str(SOURCE),
            "HOST_OSTYPE=Darwin-27-arm64", "EXTERNAL_TOOLCHAIN=/sdk",
            "EMBER_LLVM_SOURCE=" + str(self.llvm), *variables]
        for name in names:
            command.extend(("-V", "${" + name + "}"))
        return subprocess.run(command, cwd=self.work, text=True,
            capture_output=True, timeout=30)

    def test_libc_and_kernel_select_the_same_common_compiler_rt(self):
        for consumer in ("lib/libc/compiler_rt/Makefile.inc",
                         "sys/lib/libkern/Makefile.compiler-rt"):
            for family in ("clang", "gcc"):
                with self.subTest(consumer=consumer, family=family):
                    result = self.query(consumer, ("COMPILER_RT_SRCDIR",),
                        "EMBER_COMPILER_FAMILY=" + family)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.strip(),
                                     str(self.llvm / "compiler-rt"))

    def test_prepared_source_inventory(self):
        if PREPARED_LLVM is None:
            self.skipTest("pass --prepared-llvm for actual common source inventory")
        targets = (("evbarm", "aarch64", "aarch64"),
                   ("amd64", "x86_64", "x86_64"),
                   ("evbarm", "earmv7hf", "arm"),
                   ("macppc", "powerpc", "powerpc"),
                   ("sparc64", "sparc64", "sparc"),
                   ("i386", "i386", "i386"))
        for machine, arch, cpu in targets:
            for consumer in ("lib/libc/compiler_rt/Makefile.inc",
                             "sys/lib/libkern/Makefile.compiler-rt"):
                with self.subTest(architecture=arch, consumer=consumer):
                    result = self.query(consumer, ("SRCS",),
                        "EMBER_LLVM_SOURCE=" + str(PREPARED_LLVM),
                        "MACHINE=" + machine, "MACHINE_ARCH=" + arch,
                        "MACHINE_CPU=" + cpu, "LIBC_MACHINE_CPU=" + cpu,
                        "LIBC_MACHINE_ARCH=" + arch)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    builtins = PREPARED_LLVM / "compiler-rt/lib/builtins"
                    arch_paths = ((builtins / "ppc",) if cpu == "powerpc" else
                                  (builtins / cpu, builtins / arch))
                    paths = (*arch_paths, builtins,
                             PREPARED_LLVM / "compiler-rt/lib/profile")
                    for name in result.stdout.split():
                        # These divide-by-zero hooks are native OS ABI code.
                        if consumer.startswith("sys/") and cpu == "arm" and name in (
                                "__aeabi_idiv0.c", "__aeabi_ldiv0.c"):
                            continue
                        self.assertTrue(any((path / name).is_file() for path in paths),
                                        "missing common source: " + name)
        self.assertTrue((PREPARED_LLVM /
            "compiler-rt/lib/profile/InstrProfilingRuntime.cpp").is_file())

    def test_bad_common_source_never_uses_the_bundled_copy(self):
        for selected in ("relative", str(self.work / "missing"),
                         str(self.work / "bad source")):
            with self.subTest(selected=selected):
                result = self.query("lib/libc/compiler_rt/Makefile.inc",
                    ("COMPILER_RT_SRCDIR",), "EMBER_LLVM_SOURCE=" + selected)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn("EMBER_LLVM_SOURCE", result.stderr)

    def test_binary128_exception_policy_is_source_local(self):
        consumer = "lib/libc/compiler_rt/Makefile.inc"
        result = self.query(consumer, ("CPPFLAGS.comparetf2.c", "CPPFLAGS"))
        self.assertEqual(result.returncode, 0, result.stderr)
        local, global_flags = result.stdout.splitlines()
        self.assertIn("-DCOMPILER_RT_NETBSD_AARCH64_FENV", local.split())
        self.assertNotIn("COMPILER_RT_NETBSD_AARCH64_FENV", global_flags)
        result = self.query(consumer, ("CPPFLAGS.comparetf2.c",), "MKSOFTFLOAT=yes")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("COMPILER_RT_NETBSD_AARCH64_FENV", result.stdout)

    def test_common_unwind_never_falls_back_to_old_sources(self):
        consumer = "sys/lib/libunwind/Makefile.inc"
        result = self.query(consumer, ("SRCS",))
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("EMBER_LIBUNWIND_ARCHIVE", result.stderr)
        archive = self.work / "libunwind.a"
        archive.write_bytes(b"!<arch>\n")
        result = self.query(consumer, ("SRCS", "OBJS", "INCS"),
            "EMBER_LIBUNWIND_ARCHIVE=" + str(archive))
        self.assertEqual(result.returncode, 0, result.stderr)
        sources, objects, headers = result.stdout.splitlines()
        self.assertNotIn("libunwind.cxx", sources)
        self.assertNotIn("unwind_registers.S", sources)
        self.assertIn("llvm_unwind.o", objects.split())
        self.assertIn(str(self.llvm / "libunwind/include/unwind.h"), headers.split())
        self.assertIn(str(self.llvm / "libunwind/include/__libunwind_config.h"), headers.split())
        self.assertIn(str(self.llvm / "libunwind/include/unwind_itanium.h"), headers.split())
        self.assertIn(str(self.llvm / "libunwind/include/unwind_arm_ehabi.h"), headers.split())
        for target in ("llvm_unwind.o", "llvm_unwind.pico"):
            result = self.query(consumer, (),
                "EMBER_LIBUNWIND_ARCHIVE=" + str(archive), "-n", target)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("/sdk/bin/ld.lld -r -o " + target + ".tmp --whole-archive " +
                          str(archive) + " --no-whole-archive", result.stdout)
            self.assertNotIn("libunwind.cxx", result.stdout)
        for setting in ("MKPROFILE=yes", "MKDEBUGLIB=yes"):
            result = self.query(consumer, ("OBJS",),
                "EMBER_LIBUNWIND_ARCHIVE=" + str(archive), setting)
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("MKPROFILE=no and MKDEBUGLIB=no", result.stderr)
        result = self.query(consumer, ("HAVE_LLVM_UNWIND",),
            "EMBER_LIBUNWIND_ARCHIVE=" + str(archive))
        self.assertEqual(result.stdout.strip(), "yes")

    def test_header_and_object_stages_precede_the_unwind_archive(self):
        for goal in ("includes", "obj"):
            with self.subTest(goal=goal):
                result = self.query("sys/lib/libunwind/Makefile.inc",
                    ("INCS", "OBJS", "HAVE_LLVM_UNWIND"), goal)
                self.assertEqual(result.returncode, 0, result.stderr)
                headers, objects, selected = result.stdout.splitlines()
                self.assertIn(str(self.llvm / "libunwind/include/unwind.h"), headers.split())
                self.assertNotIn("llvm_unwind.o", objects.split())
                self.assertEqual(selected, "yes")

        result = self.query("sys/lib/libunwind/Makefile.inc", ("OBJS",),
            "includes", "all")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("EMBER_LIBUNWIND_ARCHIVE", result.stderr)

    def test_common_profile_sources_keep_the_libc_gcda_abi(self):
        names = ("GENERIC_SRCS", "CPPFLAGS.GCDAProfiling.c", "CPPFLAGS")
        result = self.query("lib/libc/compiler_rt/Makefile.inc", names)
        self.assertEqual(result.returncode, 0, result.stderr)
        sources, local, global_flags = result.stdout.splitlines()
        for source in ("InstrProfilingInternal.c", "InstrProfilingMerge.c",
                       "InstrProfilingMergeFile.c", "InstrProfilingNameVar.c",
                       "InstrProfilingVersionVar.c"):
            self.assertIn(source, sources.split())
        self.assertIn("-DCOMPILER_RT_LIBC_LEGACY_GCOV", local.split())
        self.assertNotIn("COMPILER_RT_LIBC_LEGACY_GCOV", global_flags)

    def runtime_options(self):
        stage = self.work / "runtime"
        for name in ("include/c++/v1/vector", "include/c++/v1/__config_site",
                     "lib/libc++.a", "lib/libc++.so", "lib/libc++abi.a",
                     "lib/libc++abi.so", "lib/libunwind.a", "lib/libunwind.so"):
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        resource = stage / "lib/clang/23"
        resource.mkdir(parents=True)
        return ("EMBER_EXTERNAL_LLVM_RUNTIME=" + str(stage),
                "EMBER_LLVM_RESOURCE_DIR=" + str(resource))

    def bootstrap_options(self):
        stage = self.work / "bootstrap"
        (stage / "lib/clang/23").mkdir(parents=True)
        (stage / "lib/libunwind.a").touch()
        return ("EMBER_EXTERNAL_LLVM_RUNTIME=" + str(stage),
                "EMBER_LLVM_RESOURCE_DIR=" + str(stage / "lib/clang/23"),
                "EMBER_LLVM_RUNTIME_PHASE=bootstrap", "MKCXX=no")

    def test_bootstrap_c_libraries_link_the_selected_runtime(self):
        options = self.bootstrap_options()
        for library in ("m", "pthread"):
            with self.subTest(library=library):
                variables = ("LIB=" + library, "SHLIB_MAJOR=1", "SHLIB_MINOR=0",
                             "SRCS=probe.c", *options)
                result = self.query("share/mk/bsd.lib.mk",
                    ("_LIB.so.full", "_MAINLIBDEPS", "CXXFLAGS"), *variables)
                self.assertEqual(result.returncode, 0, result.stderr)
                target, dependencies, cxxflags = result.stdout.splitlines()
                self.assertNotIn("include/c++", cxxflags)
                result = self.query("share/mk/bsd.lib.mk", (), *variables,
                    "-n", target, fixture_extra=".MADE: " + dependencies + "\n" +
                        " ".join(p for p in dependencies.split() if p.startswith("/")) + ":\n")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("/sdk/bin/clang --target=aarch64--netbsd", result.stdout)
                self.assertIn("--rtlib=compiler-rt --unwindlib=libunwind", result.stdout)
                self.assertIn("-resource-dir=" + str(self.work / "bootstrap/lib/clang/23"), result.stdout)
                self.assertNotIn("-lgcc", result.stdout)
                self.assertNotIn("include/c++", result.stdout)
        for setting in ("MKCXX=yes", "EMBER_LLVM_RUNTIME_PHASE=unknown", "LIBISCXX=yes"):
            result = self.query("share/mk/bsd.lib.mk", ("LDFLAGS",), "LIB=probe",
                *options, setting)
            self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_application_runtime_keeps_os_crt_and_explicit_driver_ordering(self):
        names = ("CXX", "CXXFLAGS", "MKDEPCXXFLAGS", "LDFLAGS",
                 "_GCC_CRTBEGIN", "_GCC_CRTEND", "LIBCXX")
        result = self.query("share/mk/bsd.prog.mk", names,
            "PROG_CXX=probe", *self.runtime_options())
        self.assertEqual(result.returncode, 0, result.stderr)
        value = dict(zip(names, result.stdout.splitlines()))
        self.assertIn("-stdlib=libc++", value["CXX"].split())
        for name in ("CXXFLAGS", "MKDEPCXXFLAGS"):
            self.assertIn("-nostdinc++", value[name].split())
            self.assertIn(str(self.work / "runtime/include/c++/v1"), value[name])
        for option in ("--rtlib=compiler-rt", "--unwindlib=libunwind",
                       "-Wl,-rpath,/usr/pkg/llvm/lib"):
            self.assertIn(option, value["LDFLAGS"].split())
        self.assertIn("-resource-dir=" + str(self.work / "runtime/lib/clang/23"),
                      value["LDFLAGS"].split())
        self.assertEqual(value["_GCC_CRTBEGIN"], "/target/root/usr/lib/crtbegin.o")
        self.assertEqual(value["_GCC_CRTEND"], "/target/root/usr/lib/crtend.o")
        self.assertEqual(value["LIBCXX"], str(self.work / "runtime/lib/libc++.a"))

    def test_runtime_does_not_pollute_host_or_freestanding_rules(self):
        options = self.runtime_options()
        for setting in ("HOSTPROG=probe", "CPPFLAGS=-D_KERNEL",
                        "CPPFLAGS=-D_STANDALONE", "LDFLAGS=-nostdlib"):
            with self.subTest(setting=setting):
                result = self.query("share/mk/bsd.prog.mk",
                    ("CXX", "CXXFLAGS", "LDFLAGS"), "PROG=probe", setting, *options)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn(str(self.work / "runtime"), result.stdout)
                self.assertNotIn("-stdlib=libc++", result.stdout)

    def test_cxx_library_does_not_select_an_in_tree_runtime(self):
        result = self.query("share/mk/bsd.lib.mk", ("LIBCC", "LIBDPLIBS", "DPADD"),
            "LIB=probe", "LIBISCXX=yes", "SRCS=probe.cc", *self.runtime_options())
        self.assertEqual(result.returncode, 0, result.stderr)
        compiler, dependencies, artifacts = result.stdout.splitlines()
        self.assertIn("-stdlib=libc++", compiler.split())
        self.assertNotIn("external/", dependencies)
        self.assertIn(str(self.work / "runtime/lib/libc++.a"), artifacts.split())

    def test_runtime_rejects_mixed_families_and_incomplete_stage(self):
        options = self.runtime_options()
        for setting in ("EMBER_COMPILER_FAMILY=gcc", "MKLIBCXX=yes", "MLIBDIR=compat",
                        "EMBER_LLVM_RESOURCE_DIR=relative"):
            with self.subTest(setting=setting):
                result = self.query("share/mk/bsd.prog.mk", ("LDFLAGS",),
                    "PROG=probe", *options, setting)
                self.assertNotEqual(result.returncode, 0, result.stdout)
        (self.work / "runtime/include/c++/v1/__config_site").unlink()
        result = self.query("share/mk/bsd.prog.mk", ("LDFLAGS",), "PROG=probe", *options)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("__config_site", result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", required=True, type=Path)
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--prepared-llvm", type=Path)
    args, remaining = parser.parse_known_args()
    MAKE = args.make.resolve()
    SOURCE = args.source.resolve()
    PREPARED_LLVM = args.prepared_llvm.resolve() if args.prepared_llvm else None
    unittest.main(argv=[sys.argv[0], *remaining])
