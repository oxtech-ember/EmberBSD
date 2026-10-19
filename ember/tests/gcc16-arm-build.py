#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check ARMv7 NEON objects and separate GCC16 Objective-C dependencies."""

import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--cc", type=Path, required=True, help="GCC16 C compiler without cc1obj (negative control)")
    parser.add_argument("--objc", type=Path, required=True)
    parser.add_argument("--tools", type=Path, required=True)
    parser.add_argument("--sysroot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    src, out = args.source.resolve(), args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error("--output must be empty")
    out.mkdir(parents=True, exist_ok=True)
    records = []

    def run(name, command, cwd=out, expect=0):
        command = list(map(str, command))
        env = os.environ.copy()
        for key in ("MAKEFLAGS", "MAKEOBJDIR", "MAKEOBJDIRPREFIX", "CC", "CFLAGS", "CPPFLAGS"):
            env.pop(key, None)
        env["LC_ALL"] = "C"
        result = subprocess.run(command, cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (name + ".log")).write_text(result.stdout)
        records.append({"name": name, "argv": command, "returncode": result.returncode})
        (out / "commands.json").write_text(json.dumps(records, indent=2) + "\n")
        if (result.returncode == 0) != (expect == 0):
            raise RuntimeError(f"{name}: unexpected status {result.returncode}\n{result.stdout}")
        return result.stdout

    targets = []
    for name, compiler in (("cc", args.cc), ("objc", args.objc)):
        version = run(name + "-version", [compiler, "-dumpfullversion"]).strip()
        targets.append(run(name + "-target", [compiler, "-dumpmachine"]).strip())
        if not version.startswith("16."):
            parser.error(f"{name}: requires GCC16, got {version}")
    if len(set(targets)) != 1 or not targets[0].startswith("arm"):
        parser.error("requires matching ARM GCC16 frontends")
    make = [args.tools / "bin/nbmake", "-m", src / "share/mk",
            "MACHINE=evbarm", "MACHINE_ARCH=earmv7hf", "USETOOLS=yes", "MAKECONF=/dev/null",
            f"TOOLDIR={args.tools.resolve()}", f"NETBSDSRCDIR={src}",
            f"DESTDIR={args.sysroot.resolve()}", f"EXTERNAL_TOOLCHAIN={args.cc.parent.parent.resolve()}",
            f"CC={args.cc.resolve()}", f"OBJC={args.objc.resolve()}", f"MAKEOBJDIR={out}"]
    for kind, units in (("aes", ("aes_neon.c", "aes_neon_subr.c")), ("chacha", ("chacha_neon.c",))):
        makefile = src / "tests/sys/crypto" / kind / "Makefile"
        for unit in units:
            expression = "${COMPILE.c} ${COPTS." + unit + "}"
            command = shlex.split(run(unit + "-flags", make + ["-f", makefile, "-V", expression]))
            source = src / "sys/crypto" / kind / "arch/arm" / unit
            command += [str(source), "-o", str(out / (unit + ".o"))]
            run(unit + "-compile", command)
            # The failure must identify the missing ISA, not a missing header.
            before = [flag for flag in command if flag != "-mfpu=neon"]
            before[-1] = str(out / (unit + ".negative.o"))
            negative = run(unit + "-without-neon", before, expect=1)
            if "enable NEON instructions" not in negative and "target specific option mismatch" not in negative:
                raise RuntimeError(f"{unit}: negative control did not reach the NEON diagnostic")
        for arch in ("earmv7", "aarch64", "x86_64"):
            unit = units[0]
            flags = run(kind + "-flags-" + arch,
                        make + [f"MACHINE_ARCH={arch}", "-f", makefile,
                                "-V", "${COPTS." + unit + "}"])
            if ("-mfpu=neon" in shlex.split(flags)) != (arch == "earmv7"):
                raise RuntimeError(f"unexpected {arch} flags: {flags}")

    (out / "value.h").write_text("#define VALUE 7\n")
    (out / "objc-probe.m").write_text(
        '#include "value.h"\n__attribute__((objc_root_class))\n'
        '@interface Probe\n- (int)value;\n@end\n'
        '@implementation Probe\n- (int)value { return VALUE; }\n@end\n')
    (out / "c-probe.c").write_text('#include "value.h"\nint probe(void) { return VALUE; }\n')
    (out / "Makefile").write_text('SRCS=objc-probe.m c-probe.c\n.include <bsd.dep.mk>\n')
    run("objc-object", [args.objc, "-c", out / "objc-probe.m", "-o", out / "probe.o"])
    run("objc-dependency", make + ["objc-probe.d"])
    if "value.h" not in (out / "objc-probe.d").read_text():
        raise RuntimeError("Objective-C dependency omitted the included header")
    (out / "objc-probe.d").unlink()
    # Override only the Objective-C provider; C dependency generation must work.
    normal = run("c-provider", make + ["-V", "${MKDEP}"]).strip()
    negative = run("objc-wrong-provider", make + ["objc-probe.d", f"MKDEPOBJC={normal}"], expect=1)
    if "cc1obj" not in negative and "Objective-C compiler not installed" not in negative:
        raise RuntimeError("Objective-C negative control did not identify the missing frontend")
    run("c-dependency", make + ["c-probe.d", "MKDEPOBJC=false"])
    if "value.h" not in (out / "c-probe.d").read_text():
        raise RuntimeError("C dependency omitted the included header")
    print("PASS: ARMv7 NEON objects, architecture guards, Objective-C/C dependency providers")
    print("Object and dependency checks only; no target executable or full base build.")


if __name__ == "__main__":
    main()
