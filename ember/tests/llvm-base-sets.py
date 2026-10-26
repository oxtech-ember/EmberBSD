#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Exercise the real set parser/checker with an external runtime list directory.

Pass the complete OS native-tools routing directory created by SDK. This test
does not select host utilities or compile a target; all temporary data stays in
the caller's TMPDIR. Its small payload tests installation paths, not ELF/ABI.
"""

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
TOOLS = None
COMPILER = None
NATIVE_TOOLS = None
GZIP = None


class LLVMBaseSetsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ember-llvm-sets-")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.destination = self.work / "root"
        self.destination.mkdir()
        self.overlay = self.work / "sets"
        for directory in (SOURCE / "distrib/sets/lists").iterdir():
            if directory.is_dir():
                path = self.overlay / "lists" / directory.name
                path.mkdir(parents=True)
                (path / "mi").write_text("")
        files = {"usr/pkg/llvm/include/c++/v1/vector": "// header fixture\n",
                 "usr/pkg/llvm/lib/libc++.so.1": "shared file fixture\n"}
        links = {"usr/include/c++/vector": "../../pkg/llvm/include/c++/v1/vector",
                 "usr/lib/libc++.so.1": "../pkg/llvm/lib/libc++.so.1"}
        for name, data in files.items():
            path = self.destination / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data)
        for name, target in links.items():
            path = self.destination / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.symlink_to(target)
        directories = ["."] + ["./" + str(path.relative_to(self.destination))
            for path in self.destination.rglob("*") if path.is_dir()]
        base = directories + ["./" + name for name in (*files, *links) if "/lib/" in name]
        comp = ["./" + name for name in (*files, *links) if "/include/" in name]
        for name, rows in (("base", base), ("comp", comp)):
            (self.overlay / "lists" / name / "mi").write_text(
                "".join(path + "\t" + name + "-sys-root\n" for path in sorted(rows)))
        # Retain one actual old record as a control for the common-provider
        # selector. The modern payload deliberately has no such header.
        with (SOURCE / "distrib/sets/lists/comp/mi").open() as stream:
            legacy = next(line for line in stream
                          if line.startswith("./usr/include/c++/__availability\t"))
        with (self.overlay / "lists/comp/mi").open("a") as stream:
            stream.write(legacy)
        # Metadata queries include normal program makefiles. Give them a
        # complete, separate runtime layout; these files are not target code
        # and do not participate in the destination inventory under test.
        runtime = self.work / "runtime"
        for name in ("include/c++/v1/vector", "include/c++/v1/__config_site",
                     "lib/libc++.a", "lib/libc++.so", "lib/libc++abi.a",
                     "lib/libc++abi.so", "lib/libunwind.a", "lib/libunwind.so"):
            path = runtime / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        resource = runtime / "lib/clang/23"
        resource.mkdir(parents=True)
        make = self.work / "make"
        arguments = [str(TOOLS / "make"), "-m", str(SOURCE / "share/mk"),
            "NETBSDSRCDIR=" + str(SOURCE), "MAKECONF=/dev/null", "MACHINE=evbarm",
            "MACHINE_ARCH=aarch64", "MACHINE_GNU_PLATFORM=aarch64--netbsd",
            "EMBER_COMPILER_FAMILY=clang", "EXTERNAL_TOOLCHAIN=" + str(COMPILER),
            "TOOLCHAIN_MISSING=yes", "USETOOLS=yes", "MKCOMPAT=no", "MKX11=no",
            "MKKMOD=no", "MKDTB=no", "MKLLVM=no", "MKLLVMRT=no", "MKLIBCXX=yes",
            "EMBER_EXTERNAL_LLVM_RUNTIME=" + str(runtime),
            "EMBER_LLVM_RESOURCE_DIR=" + str(resource), "MKREPRO=yes",
            "MKCXX=yes", "MKDEBUG=no", "MKDEBUGLIB=no",
            "NETBSDOBJDIR=" + str(self.work), "MAKEOBJDIR=" + str(self.work)]
        make.write_text("#!" + str(TOOLS / "sh") + "\nexec " + shlex.join(arguments) + ' "$@"\n')
        make.chmod(0o755)
        self.environment = {name: value for name, value in os.environ.items()
                            if name in ("HOME", "TMPDIR", "TZ", "LANG", "LC_ALL", "PATH")}
        # The SDK runner provides routing followed by its native prefix paths.
        # Keep those selected paths: sets also uses coreutils such as comm/join.
        if self.environment.get("PATH", "").split(os.pathsep)[0] != str(TOOLS):
            raise RuntimeError("run this fixture through the selected SDK native-OS environment")
        self.environment.update(MAKE=str(make), HOST_SH=str(TOOLS / "sh"),
            DESTDIR=str(self.destination), EGREP=str(TOOLS / "grep") + " -E",
            FGREP=str(TOOLS / "grep") + " -F", MAKEFLAGS="")

    def check(self, arguments=None, environment=None):
        if arguments is None:
            arguments = ["-s", str(self.overlay), "-L", "base"]
        return subprocess.run([str(TOOLS / "sh"), str(SOURCE / "distrib/sets/checkflist"),
            *arguments], env=environment or self.environment,
            cwd=self.work, text=True, capture_output=True, timeout=60)

    def test_environment_default_and_explicit_override(self):
        environment = dict(self.environment, EMBER_SETS_SOURCE=str(self.overlay))
        result = self.check(["-L", "base"], environment)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "", result.stdout + result.stderr)
        environment["EMBER_SETS_SOURCE"] = str(self.work / "unselected")
        result = self.check(environment=environment)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "", result.stdout + result.stderr)

    def test_unknown_option_is_rejected(self):
        result = self.check(["-Q"])
        self.assertNotEqual(result.returncode, 0)

    def test_real_archives_use_overlay_lists_and_owned_temporary_directory(self):
        if NATIVE_TOOLS is None or GZIP is None:
            self.skipTest('provide built native OS tools and selected gzip for archive execution')
        temporary = self.work / 'archive-tmp'; temporary.mkdir()
        output = self.work / 'archives'; output.mkdir()
        setfiles = self.work / 'setfiles'; setfiles.mkdir()
        # Refuse an old producer's external temporary path before creating it.
        mktemp = self.work / 'checked-mktemp'
        selected_mktemp = shutil.which('mktemp', path=self.environment['PATH'])
        self.assertIsNotNone(selected_mktemp, 'the selected native prefix must contain mktemp')
        mktemp.write_text('#!' + str(TOOLS / 'sh') + '\n'
            'case "$2" in "$TMPDIR/"*) exec ' + shlex.quote(selected_mktemp) + ' "$@";;\n'
            '*) printf "%s\\n" "temporary path escaped TMPDIR" >&2;\n'
            'printf "%s\\n" "$TMPDIR/refused"; exit 1;; esac\n')
        mktemp.chmod(0o755)
        environment = dict(self.environment, TMPDIR=str(temporary), MKTEMP=str(mktemp),
            MTREE=str(NATIVE_TOOLS / 'nbmtree'), PAX=str(NATIVE_TOOLS / 'nbpax'),
            COMPRESS_PROGRAM=str(GZIP), EMBER_SETS_SOURCE=str(self.overlay))
        result = subprocess.run([str(TOOLS / 'sh'), str(SOURCE / 'distrib/sets/maketars'),
            '-d', str(self.destination), '-t', str(output), '-F', str(setfiles),
            '-N', str(SOURCE / 'etc'), 'base', 'comp'], env=environment,
            cwd=self.work, text=True, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(list(temporary.iterdir()), [])
        with tarfile.open(output / 'comp.tgz') as archive:
            members = {member.name.removeprefix('./'): member for member in archive}
            self.assertEqual(members['usr/include/c++/vector'].linkname,
                             '../../pkg/llvm/include/c++/v1/vector')
            self.assertNotIn('usr/include/c++/__availability', members)
            self.assertEqual(archive.extractfile(members['usr/pkg/llvm/include/c++/v1/vector']).read(),
                             b'// header fixture\n')
        with tarfile.open(output / 'base.tgz') as archive:
            members = {member.name.removeprefix('./'): member for member in archive}
            self.assertEqual(members['usr/lib/libc++.so.1'].linkname, '../pkg/llvm/lib/libc++.so.1')

    def test_common_provider_omits_old_static_header_records(self):
        result = subprocess.run([str(TOOLS / "sh"), str(SOURCE / "distrib/sets/makeflist"),
            "-s", str(self.overlay), "comp"], env=self.environment,
            cwd=self.work, text=True, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "", result.stdout + result.stderr)
        self.assertIn("./usr/include/c++/vector", result.stdout.splitlines())
        self.assertNotIn("./usr/include/c++/__availability", result.stdout.splitlines())

    def test_external_lists_match_real_payload_and_preserve_refusals(self):
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "", result.stdout + result.stderr)
        header = self.destination / "usr/include/c++/vector"
        header.unlink()
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("./usr/include/c++/vector", result.stdout)
        self.assertIn("missing files", result.stdout)
        header.symlink_to("../../pkg/llvm/include/c++/v1/vector")
        (header.parent / "old-libcxxrt-header").write_text("unexpected legacy header\n")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("./usr/include/c++/old-libcxxrt-header", result.stdout)
        self.assertIn("extra files", result.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", required=True, type=Path,
                        help="complete SDK OS native-tools routing directory")
    parser.add_argument("--source", default=SOURCE, type=Path)
    parser.add_argument("--compiler", required=True, type=Path,
                        help="selected LLVM compiler prefix for make metadata")
    parser.add_argument("--native-tools", type=Path,
                        help="built native OS TOOLDIR/bin containing nbmtree and nbpax")
    parser.add_argument("--gzip", type=Path, help="selected native SDK gzip executable")
    args, remaining = parser.parse_known_args()
    TOOLS = args.tools.resolve()
    SOURCE = args.source.resolve()
    COMPILER = args.compiler.resolve()
    NATIVE_TOOLS = args.native_tools.resolve() if args.native_tools else None
    GZIP = args.gzip.resolve() if args.gzip else None
    unittest.main(argv=[sys.argv[0], *remaining])
