#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Check that gallium_dri exports only entry points of the drivers it builds.

The xsrc Mesa targets/dri/dri.sym names the winsys entry points of every
gallium driver. ld.lld refuses a version script global that the link does
not define, so an AArch64 X11 build stopped at gallium_dri.so on
ac_init_shared_llvm_once, amdgpu_winsys_create, fd_drm_screen_create and
radeon_drm_winsys_create. Evaluates the real share/mk with the selected make
and LLVM prefix, writes the version script the link uses, and requires each
exported name to be defined by a source in the library's SRCS.
"""

import argparse
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]
GALLIUM = SOURCE / "external/mit/xorg/lib/gallium"
ARGS = None


def make(objdir, *words):
    command = [str(ARGS.make), "-m", str(SOURCE / "share/mk"), "-C", str(GALLIUM),
               "MAKEOBJDIR=" + str(objdir),
               "MACHINE=" + ARGS.machine, "MACHINE_ARCH=" + ARGS.machine_arch, "USETOOLS=yes",
               "TOOLCHAIN_MISSING=yes", "MKBINUTILS=no", "EMBER_COMPILER_FAMILY=clang",
               "EXTERNAL_TOOLCHAIN=" + str(ARGS.toolchain), "TOOLDIR=" + str(ARGS.tooldir),
               "NETBSDSRCDIR=" + str(SOURCE), "DESTDIR=" + str(ARGS.sysroot), "MKX11=yes",
               "X11SRCDIR=" + str(ARGS.x11_source)] + list(words)
    env = {"PATH": "/usr/bin:/bin", "HOME": tempfile.gettempdir()}
    result = subprocess.run(command, text=True, capture_output=True, env=env, timeout=120)
    if result.returncode:
        raise AssertionError(result.stderr[-4000:])
    return result.stdout


def version_script_globals(text):
    body = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    match = re.search(r"global:(.*?)local:", body, flags=re.S)
    if not match:
        raise AssertionError("no global section in version script:\n" + text)
    return [name.strip() for name in match.group(1).split(";") if name.strip()]


def defining_sources(name, tree):
    # A definition starts a line (Mesa puts the return type on the line above)
    # or follows a type on the same line, and is not a declaration.
    pattern = re.compile(r"^(?:[A-Za-z_][\w \t\*]*[ \t\*])?" + re.escape(name) +
                         r"\s*(?:\(|\[|=)(?!.*;\s*$)", re.M)
    found = set()
    for path in tree.rglob("*.c"):
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        if name in text and pattern.search(text):
            found.add(path.name)
    return found


class GalliumDriVersionScriptTests(unittest.TestCase):
    def test_exported_entry_points_are_built(self):
        # MKLLVMRT selects the LLVM drivers (llvmpipe, radeon, radeonsi).
        for llvmrt in ("no", "yes"):
            with self.subTest(MKLLVMRT=llvmrt):
                self.check_configuration("MKLLVMRT=" + llvmrt)

    def check_configuration(self, *settings):
        with tempfile.TemporaryDirectory() as objdir:
            ldflags, srcs = [shlex.split(line) for line in
                             make(objdir, *settings, "-V", "${LDFLAGS}", "-V", "${SRCS}").splitlines()]
            scripts = [word.split("=", 1)[1] for word in ldflags
                       if word.startswith("-Wl,--version-script=")]
            self.assertEqual(len(scripts), 1, ldflags)
            script = Path(scripts[0])
            if not script.is_absolute() or str(script).startswith(objdir):
                make(objdir, *settings, script.name)
            names = version_script_globals(script.read_text())
            self.assertIn("__driDriverGetExtensions*", names)
            tree = ARGS.x11_source / "external/mit/MesaLib/dist/src"
            built = set(Path(name).name for name in srcs)
            for name in names:
                if "*" in name or "?" in name:
                    continue
                with self.subTest(settings=" ".join(settings), symbol=name):
                    sources = defining_sources(name, tree)
                    self.assertTrue(sources, name + ": no definition in xsrc")
                    self.assertTrue(sources & built,
                                    "%s exported but defined only in %s, none built" %
                                    (name, ", ".join(sorted(sources))))


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", required=True, type=Path, help="host bmake")
    parser.add_argument("--toolchain", required=True, type=Path, help="LLVM prefix (EXTERNAL_TOOLCHAIN)")
    parser.add_argument("--tooldir", required=True, type=Path)
    parser.add_argument("--x11-source", required=True, type=Path, help="prepared xsrc tree")
    parser.add_argument("--sysroot", required=True, type=Path, help="target base destination")
    parser.add_argument("--machine", default="evbarm")
    parser.add_argument("--machine-arch", default="aarch64")
    ARGS, rest = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
