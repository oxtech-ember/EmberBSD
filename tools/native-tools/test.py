#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# This file was developed with AI assistance.

"""Test build.sh routing with synthetic tools. Do not build native tools."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[2]
SHELL = None
BUILD_SH = SOURCE / "build.sh"

# Each executable is a synthetic role. The only host executable invoked by
# these fixtures is the explicitly selected test shell and this Python.
UTILITY = r'''
import json, os, re, shlex, shutil, sys
from pathlib import Path
name = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ["FIXTURE_LOG"], "a") as stream:
    stream.write(json.dumps({"command": name, "argv": args,
        "cwd": os.getcwd(), "env": {key: os.environ.get(key) for key in
        ("PATH", "HOST_CC", "HOST_CXX", "HOST_SH", "HOST_YACC", "HOST_MV_CP", "HOST_MV_RM", "CONFIG_SHELL", "BSHELL",
         "SHELL", "HOST_CFLAGS", "HOST_LDFLAGS", "EMBER_COMPILER_FAMILY",
         "EMBER_HOST_COMPILER_FAMILY", "EXTERNAL_TOOLCHAIN")}}) + "\n")
if name == "make":
    if "-f-" in args:
        text = sys.stdin.read()
        key = re.search(r"echo \$\{([^}]+)\}", text).group(1)
        values = {"MKOBJDIRS": "no", "MKUNPRIVED": "yes", "MKUPDATE": "yes",
            "TOOLCHAIN_MISSING": "no",
            "EXTERNAL_TOOLCHAIN": os.environ.get("EXTERNAL_TOOLCHAIN", ""),
            "MKLLVM": os.environ.get("FIXTURE_MKLLVM", "yes"),
            "MKLLVMRT": os.environ.get("FIXTURE_MKLLVMRT", "yes"),
            "MAKECONF": os.environ["FIXTURE_ROOT"] + "/no-mk.conf",
            "RELEASEMACHINEDIR": "amd64"}
        values["TOOLCHAIN_MISSING"] = os.environ.get(
            "FIXTURE_TOOLCHAIN_MISSING", values["TOOLCHAIN_MISSING"])
        print(values.get(key, os.environ.get(key, "")))
    sys.exit(0)
if name == "cmp":
    sys.exit(0 if Path(args[-2]).read_bytes() == Path(args[-1]).read_bytes() else 1)
if name == "readlink":
    print(os.readlink(args[0]))
elif name == "uname":
    print({"-s": "Darwin", "-r": "fixture", "-m": "arm64",
        "-p": "arm64"}[args[0]])
elif name == "id":
    print("501")
elif name == "pwd":
    print(os.getcwd())
elif name == "dirname":
    print(os.path.dirname(args[0]) or ".")
elif name == "printf" and args[0] == "%s\\n":
    for arg in args[1:]:
        print(arg)
elif name == "date":
    print("fixture-date")
elif name == "mkdir":
    for arg in args:
        if not arg.startswith("-"):
            Path(arg).mkdir(parents="-p" in args, exist_ok="-p" in args)
elif name == "rm":
    for arg in args:
        if arg.startswith("-"):
            continue
        path = Path(arg)
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        elif path.exists() or path.is_symlink():
            path.unlink()
elif name == "cp":
    shutil.copyfile(args[-2], args[-1])
elif name == "chmod":
    Path(args[-1]).chmod(0o755)
elif name == "cat":
    sys.stdout.write("".join(Path(a).read_text() for a in args)
        if args else sys.stdin.read())
elif name == "tee":
    data = sys.stdin.read()
    with open(args[-1], "a" if "-a" in args else "w") as stream:
        stream.write(data)
    sys.stdout.write(data)
elif name == "sort":
    print("\n".join(sorted(set(sys.stdin.read().splitlines()))))
elif name == "tr" and args == ["-s", ".-", "__"]:
    sys.stdout.write(re.sub("_+", "_", sys.stdin.read().translate(
        str.maketrans({".": "_", "-": "_"}))))
elif name == "sed" and len(args) == 12:
    print(shlex.quote(sys.stdin.read().removesuffix("\n")))
elif name == "sed" and len(args) == 3 and args[0] == "-e":
    for line in Path(args[2]).read_text().splitlines():
        print("\t" + re.sub(r"^===>", "", line))
else:
    sys.exit("Unexpected fixture command: " + name + " " + repr(args))
'''


class Fixture:
    def __init__(self, test):
        self.temp = tempfile.TemporaryDirectory(prefix="ember-native-tools-")
        test.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.source = self.root / "source"
        self.routing = self.root / "routing"
        self.selected = self.root / "selected"
        self.hostile = self.root / "hostile"
        self.log = self.root / "commands.jsonl"
        self.tooldir = self.root / "tooldir"
        for path in (self.source / "usr.bin/make", self.source / "share/mk",
                     self.source / "sys/conf", self.source / "tools/native-tools",
                     self.routing, self.selected, self.hostile,
                     self.tooldir / "bin", self.root / "scratch"):
            path.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(BUILD_SH, self.source / "build.sh")
        shutil.copyfile(SOURCE / "tools/native-tools/contract",
                        self.source / "tools/native-tools/contract")
        shutil.copyfile(SOURCE / "tools/native-tools/contract",
                        self.routing / "contract")
        (self.source / "share/mk/bsd.own.mk").touch()
        (self.source / "sys/conf/osrelease.sh").write_text("echo fixture\n")
        # This sentinel must never run, even when TOOLDIR already has a make.
        self.sentinel = self.root / "ambient-ran"
        trap = f"#!{SHELL}\necho unsafe > {self.sentinel}\nexit 91\n"
        for path in (self.tooldir / "bin/nbmake", self.hostile / "make",
                     self.hostile / "sh", self.hostile / "sed"):
            path.write_text(trap)
            path.chmod(0o755)
        for line in (self.routing / "contract").read_text().splitlines()[1:]:
            command, _, _ = line.split()
            target = self.selected / command
            if command == "sh":
                target.symlink_to(SHELL)
            else:
                target.write_text(f"#!{sys.executable}\n" + UTILITY)
                target.chmod(0o755)
            (self.routing / command).symlink_to(target)
        self.env = {"PATH": str(self.hostile), "EMBER_NATIVE_TOOLS": str(self.routing),
            "HOST_CC": "/unselected/cc", "HOST_CXX": "/unselected/c++",
            "HOST_SH": "/unselected/sh", "HOST_YACC": "/unselected/yacc",
            "HOST_MV_CP": "/unselected/cp", "HOST_MV_RM": "/unselected/rm",
            "SED": str(self.hostile / "sed"),
            "TMPDIR": str(self.root / "scratch"), "MACHINE": "amd64",
            "MACHINE_ARCH": "x86_64", "TOOLDIR": str(self.tooldir),
            "DESTDIR": str(self.root / "destdir"),
            "RELEASEDIR": str(self.root / "release"),
            "FIXTURE_ROOT": str(self.root), "FIXTURE_LOG": str(self.log)}

    def run(self, *args):
        return subprocess.run([str(SHELL), "build.sh", "-U", *args],
            cwd=self.source, env=self.env, text=True, capture_output=True, timeout=30)

    def run_without_native_tools(self, *args):
        env = dict(self.env)
        env.pop("EMBER_NATIVE_TOOLS", None)
        for key in ("HOST_SH", "HOST_CC", "HOST_CXX", "HOST_CPP", "HOST_AR",
                    "HOST_LD", "HOST_RANLIB", "HOST_LN", "HOST_YACC",
                    "HOST_MV_CP", "HOST_MV_RM", "SED"):
            env.pop(key, None)
        env["PATH"] = str(self.routing)
        return subprocess.run([str(SHELL), "build.sh", "-U", *args],
            cwd=self.source, env=env, text=True, capture_output=True, timeout=30)

    def records(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]


class RoutingTests(unittest.TestCase):
    def test_real_operations_require_native_routing_before_make_bootstrap(self):
        fixture = Fixture(self)
        result = fixture.run_without_native_tools("build")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("EMBER_NATIVE_TOOLS", result.stderr)
        self.assertIn("tools/native-tools/README.md", result.stderr)
        self.assertFalse(fixture.sentinel.exists())
        self.assertFalse(any(row["command"] in ("make", "cc")
                             for row in fixture.records()))

    def test_target_build_requires_external_toolchain(self):
        fixture = Fixture(self)
        result = fixture.run("build")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("EXTERNAL_TOOLCHAIN", result.stderr)
        calls = [row for row in fixture.records() if row["command"] == "make"]
        self.assertFalse(any(row["argv"] == ["MACHINE=amd64", "MACHINE_ARCH=x86_64", "build"]
                             for row in calls))

    def test_metadata_operation_and_help_need_no_target_toolchain(self):
        fixture = Fixture(self)
        result = fixture.run("show-params")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(any(row["command"] == "make" for row in fixture.records()))
        result = fixture.run_without_native_tools("-h")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_dry_run_does_not_require_native_tools(self):
        fixture = Fixture(self)
        (fixture.tooldir / "bin/nbmake").unlink()
        (fixture.tooldir / "bin/nbmake").symlink_to(fixture.selected / "make")
        result = fixture.run_without_native_tools("-T", str(fixture.tooldir), "-n", "build")
        self.assertNotIn("operations require EMBER_NATIVE_TOOLS", result.stderr)
        self.assertNotIn("requires EXTERNAL_TOOLCHAIN", result.stderr)

    def test_tools_require_disabled_compiler_producers_on_missing_toolchain(self):
        fixture = Fixture(self)
        fixture.env["FIXTURE_TOOLCHAIN_MISSING"] = "yes"
        fixture.env["FIXTURE_MKLLVM"] = "no"
        fixture.env["FIXTURE_MKLLVMRT"] = "no"
        result = fixture.run("tools")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(any(row["argv"] == ["MACHINE=amd64", "MACHINE_ARCH=x86_64", "build_install"]
                            and row["cwd"] == str(fixture.source / "tools")
                            for row in fixture.records() if row["command"] == "make"))

    def test_tools_compiler_exception_does_not_cover_other_operations(self):
        for operation in ("libs", "kernel=GENERIC", "bootstrap-crt"):
            with self.subTest(operation=operation):
                fixture = Fixture(self)
                fixture.env["FIXTURE_TOOLCHAIN_MISSING"] = "yes"
                fixture.env["FIXTURE_MKLLVM"] = "no"
                fixture.env["FIXTURE_MKLLVMRT"] = "no"
                result = fixture.run("tools", operation)
                self.assertNotEqual(result.returncode, 0,
                    result.stdout + result.stderr)
                self.assertIn("EXTERNAL_TOOLCHAIN", result.stderr)
                self.assertFalse(any(row["argv"] == ["MACHINE=amd64",
                    "MACHINE_ARCH=x86_64", operation] for row in fixture.records()
                    if row["command"] == "make"))

    def test_tools_compiler_exception_allows_metadata_operation(self):
        fixture = Fixture(self)
        fixture.env["FIXTURE_TOOLCHAIN_MISSING"] = "yes"
        fixture.env["FIXTURE_MKLLVM"] = "no"
        fixture.env["FIXTURE_MKLLVMRT"] = "no"
        result = fixture.run("tools", "show-params")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(any(row["command"] == "make" and
            row["argv"] == ["MACHINE=amd64", "MACHINE_ARCH=x86_64", "build_install"]
            and row["cwd"] == str(fixture.source / "tools")
            for row in fixture.records()))
        self.assertTrue(any(row["command"] == "make" and
            row["argv"][-1] == "show-params" for row in fixture.records()))

    def test_tools_with_compiler_targets_require_external_toolchain(self):
        fixture = Fixture(self)
        fixture.env["FIXTURE_TOOLCHAIN_MISSING"] = "yes"
        fixture.env["FIXTURE_MKLLVM"] = "no"
        fixture.env["FIXTURE_MKLLVMRT"] = "yes"
        result = fixture.run("tools")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("EXTERNAL_TOOLCHAIN", result.stderr)
        self.assertFalse(any(row["argv"] == ["MACHINE=amd64", "MACHINE_ARCH=x86_64", "build_install"]
                             and row["cwd"] == str(fixture.source / "tools")
                             for row in fixture.records() if row["command"] == "make"))

    def test_bootstrap_headers_and_crt_dispatch(self):
        for goal in ("bootstrap-headers", "bootstrap-crt"):
            with self.subTest(goal=goal):
                fixture = Fixture(self)
                args = ("-V", "EXTERNAL_TOOLCHAIN=/selected/target", goal) \
                    if goal == "bootstrap-crt" else (goal,)
                result = fixture.run(*args)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                calls = [row for row in fixture.records() if row["command"] == "make"]
                self.assertTrue(any(row["argv"] == ["MACHINE=amd64", "MACHINE_ARCH=x86_64", goal] and
                    row["cwd"] == str(fixture.source) for row in calls))
                self.assertFalse(any(row["argv"] == ["MACHINE=amd64", "MACHINE_ARCH=x86_64", "build_install"] for row in calls))

    def assert_refused(self, fixture, message, *args):
        result = fixture.run(*args, "makewrapper")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(message, result.stderr)
        self.assertFalse(fixture.sentinel.exists())
        if fixture.log.exists():
            self.assertNotIn("make", [row["command"] for row in fixture.records()])

    def test_tools_dispatch_and_wrapper_replay(self):
        fixture = Fixture(self)
        result = fixture.run("-V", "HOST_CFLAGS=-O2 -fPIC", "-V",
            "EXTERNAL_TOOLCHAIN=/selected/target", "tools")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        records = fixture.records()
        calls = [row for row in records if row["command"] == "make"]
        self.assertTrue(any(row["argv"] == ["MACHINE=amd64", "MACHINE_ARCH=x86_64", "build_install"] and
            row["cwd"] == str(fixture.source / "tools") for row in calls))
        self.assertFalse(fixture.sentinel.exists())
        self.assertFalse({"cc", "c++", "cpp", "ar", "as", "ld", "ranlib", "yacc"} &
            {row["command"] for row in records})
        for row in calls:
            self.assertEqual(row["env"]["PATH"], str(fixture.routing))
            self.assertEqual(row["env"]["HOST_CC"], str(fixture.selected / "cc"))
            self.assertEqual(row["env"]["HOST_CXX"], str(fixture.selected / "c++"))
            self.assertEqual(row["env"]["HOST_SH"], str(fixture.selected / "sh"))
            self.assertEqual(row["env"]["HOST_YACC"], str(fixture.selected / "yacc"))
            self.assertEqual(row["env"]["HOST_MV_CP"], str(fixture.selected / "cp"))
            self.assertEqual(row["env"]["HOST_MV_RM"], str(fixture.selected / "rm"))
            self.assertEqual(row["env"]["HOST_CFLAGS"], "-O2 -fPIC")
        wrapper = fixture.tooldir / "bin/nbmake-amd64"
        content = wrapper.read_text()
        self.assertIn(f"exec {fixture.selected}/make ", content)
        self.assertNotIn('exec "${TOOLDIR}/bin/nbmake"', content)
        result = subprocess.run([str(SHELL), str(wrapper), "fixture-replay"],
            cwd=fixture.source, env=fixture.env, text=True, capture_output=True,
            timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        replay = fixture.records()[-1]
        self.assertEqual(replay["argv"], ["MACHINE=amd64", "MACHINE_ARCH=x86_64", "fixture-replay"])
        self.assertEqual(replay["env"]["HOST_YACC"], str(fixture.selected / "yacc"))
        self.assertEqual(replay["env"]["HOST_MV_CP"], str(fixture.selected / "cp"))
        self.assertEqual(replay["env"]["HOST_MV_RM"], str(fixture.selected / "rm"))
        for key in ("HOST_SH", "BSHELL", "CONFIG_SHELL", "SHELL"):
            self.assertEqual(replay["env"][key], str(fixture.selected / "sh"))

    def test_target_compiler_family_reaches_make_and_wrapper(self):
        for selection, expected in (((), "clang"), (("-c", "clang"), "clang"),
                                    (("-c", "gcc"), "gcc")):
            with self.subTest(selection=selection):
                fixture = Fixture(self)
                host_family = "gcc" if expected == "clang" else "clang"
                fixture.env["EMBER_HOST_COMPILER_FAMILY"] = host_family
                result = fixture.run(*selection, "-V",
                    "EXTERNAL_TOOLCHAIN=/selected/target", "tools")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                calls = [row for row in fixture.records() if row["command"] == "make"]
                self.assertTrue(calls)
                for call in calls:
                    self.assertEqual(call["env"]["EMBER_COMPILER_FAMILY"], expected)
                    self.assertEqual(call["env"]["EMBER_HOST_COMPILER_FAMILY"], host_family)
                wrapper = fixture.tooldir / "bin/nbmake-amd64"
                fixture.env.pop("EMBER_HOST_COMPILER_FAMILY")
                result = subprocess.run([str(SHELL), str(wrapper), "family-replay"],
                    cwd=fixture.source, env=fixture.env, text=True,
                    capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(fixture.records()[-1]["env"]["EMBER_COMPILER_FAMILY"], expected)
                self.assertEqual(fixture.records()[-1]["env"]["EMBER_HOST_COMPILER_FAMILY"], host_family)

    def test_missing_member(self):
        fixture = Fixture(self)
        (fixture.routing / "cc").unlink()
        self.assert_refused(fixture, "missing native tool: cc")

    def test_missing_printf_is_refused(self):
        fixture = Fixture(self)
        (fixture.routing / "printf").unlink()
        self.assert_refused(fixture, "missing native tool: printf")

    def test_missing_yacc_is_refused(self):
        fixture = Fixture(self)
        (fixture.routing / "yacc").unlink()
        self.assert_refused(fixture, "missing native tool: yacc")

    def test_regular_member_is_refused(self):
        fixture = Fixture(self)
        (fixture.routing / "cc").unlink()
        (fixture.routing / "cc").write_text("fixture\n")
        (fixture.routing / "cc").chmod(0o755)
        self.assert_refused(fixture, "missing native tool: cc")

    def test_wrong_contract(self):
        fixture = Fixture(self)
        with (fixture.routing / "contract").open("a") as stream:
            stream.write("unexpected\n")
        self.assert_refused(fixture, "native-tool contract does not match")

    def test_symlink_contract_is_refused(self):
        fixture = Fixture(self)
        (fixture.routing / "contract").unlink()
        (fixture.routing / "contract").symlink_to(
            fixture.source / "tools/native-tools/contract")
        self.assert_refused(fixture, "not a regular file")

    def test_relative_root_is_refused(self):
        fixture = Fixture(self)
        fixture.env["EMBER_NATIVE_TOOLS"] = "relative"
        self.assert_refused(fixture, "must be an absolute path")

    def test_path_separator_is_refused(self):
        fixture = Fixture(self)
        fixture.env["EMBER_NATIVE_TOOLS"] += ":/bin"
        self.assert_refused(fixture, "unsupported character")

    def test_relative_link_target_is_refused(self):
        fixture = Fixture(self)
        (fixture.routing / "cc").unlink()
        (fixture.routing / "cc").symlink_to("../selected/cc")
        self.assert_refused(fixture, "target must be absolute")

    def test_target_with_space_is_refused(self):
        fixture = Fixture(self)
        target = fixture.selected / "unsafe cc"
        shutil.copyfile(fixture.selected / "cc", target)
        target.chmod(0o755)
        (fixture.routing / "cc").unlink()
        (fixture.routing / "cc").symlink_to(target)
        self.assert_refused(fixture, "unsupported character in native-tool target")

    def test_selected_shell_failure_has_no_fallback(self):
        fixture = Fixture(self)
        (fixture.selected / "sh").unlink()
        (fixture.selected / "sh").write_text(f"#!{SHELL}\nexit 1\n")
        (fixture.selected / "sh").chmod(0o755)
        self.assert_refused(fixture, "failed functionality tests")

    def test_routing_overrides_are_refused(self):
        for option, variable in (("-V", "PATH=/bin"), ("-Z", "HOST_SH"),
                ("-V", "_ember_native_make=/bin/false"),
                ("-V", "SED=/bin/false"), ("-V", "HOST_YACC=/bin/false"),
                ("-Z", "HOST_YACC"), ("-V", "HOST_MV_CP=/bin/false"),
                ("-V", "HOST_MV_RM=/bin/false"), ("-Z", "HOST_MV_CP"),
                ("-Z", "HOST_MV_RM")):
            with self.subTest(variable=variable):
                fixture = Fixture(self)
                self.assert_refused(fixture, "Cannot change selected native tool",
                    option, variable)

    def test_bootstrap_rebuild_is_refused(self):
        fixture = Fixture(self)
        self.assert_refused(fixture, "Refresh make through the native-tool producer", "-r")

    def test_usetools_no_is_refused(self):
        fixture = Fixture(self)
        self.assert_refused(fixture, "require USETOOLS=yes", "-V", "USETOOLS=no")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shell", required=True, type=Path,
        help="Explicit POSIX test shell. This does not qualify it for an SDK.")
    parser.add_argument("--build-sh", type=Path, default=BUILD_SH,
        help="Source under test. Use an older source for a causal negative control.")
    options, remaining = parser.parse_known_args()
    SHELL = options.shell.absolute()
    BUILD_SH = options.build_sh.absolute()
    if not SHELL.is_file() or not os.access(SHELL, os.X_OK):
        parser.error("--shell must name an executable file")
    unittest.main(argv=[sys.argv[0], *remaining])
