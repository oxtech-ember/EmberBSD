#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: AI-assisted EmberBSD ARM standalone build regression.
"""Check ARM standalone objects and ELF layouts. This does not boot firmware."""

import argparse
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys


def run(command, *, cwd, env=None):
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f"{shlex.join(map(str, command))}\n"
                           f"{result.stdout}{result.stderr}")
    return result.stdout


def elf_segments(path):
    data = path.read_bytes()
    if data[:7] != b"\x7fELF\x01\x01\x01":
        raise RuntimeError(f"{path}: expected little-endian ELF32")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if header[2] != 40 or header[9] != 32:
        raise RuntimeError(f"{path}: expected ARM program headers")
    return [struct.unpack_from("<IIIIIIII", data, header[5] + i * header[9])
            for i in range(header[10])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--cc", required=True, help="GCC16 ARM cross compiler")
    parser.add_argument("--bmake", required=True, help="NetBSD make or nbmake")
    parser.add_argument("--objcopy", required=True, help="matching ARM GNU objcopy")
    parser.add_argument("--sysroot", type=Path, required=True,
                        help="EmberBSD ARM headers for object checks")
    parser.add_argument("--output", type=Path, required=True,
                        help="empty directory for this check's objects")
    args = parser.parse_args()
    src = args.source.resolve()
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error("--output must be empty")
    out.mkdir(parents=True, exist_ok=True)
    cc = args.cc
    version = run([cc, "-dumpfullversion"], cwd=out).strip()
    target = run([cc, "-dumpmachine"], cwd=out).strip()
    if not version.startswith("16.") or not target.startswith("arm"):
        parser.error(f"expected GCC16 ARM compiler, got {version} {target}")
    ld = run([cc, "-print-prog-name=ld"], cwd=out).strip()
    objcopy = args.objcopy
    failures = []

    def check(name, action):
        try:
            action()
        except (RuntimeError, AssertionError) as error:
            failures.append(name)
            print(f"FAIL {name}: {error}", file=sys.stderr)
        else:
            print(f"PASS {name}")

    def flags():
        env = os.environ.copy()
        env.pop("MAKEOBJDIRPREFIX", None)
        env["MAKEOBJDIR"] = str(out)
        for mode in ("yes", "no"):
            command = [args.bmake, "-C", str(src / "sys/arch/acorn32/stand/nbfs"),
                       "-m", str(src / "share/mk"), "USETOOLS=no",
                       "MACHINE=acorn32", "MACHINE_ARCH=earmv7hf",
                       f"MKMODULE={mode}", "MKMAN=no", "MKDEBUG=no", "MKCTF=no",
                       f"NETBSDSRCDIR={src}", "-V", "${CFLAGS}"]
            result = shlex.split(run(command, cwd=out, env=env))
            for flag in ("-DRISCOS_MODULE", "-fPIC"):
                assert (flag in result) == (mode == "yes"), (mode, flag, result)

    check("module flags survive the CFLAGS assignment", flags)
    # A known first word verifies that ELF headers do not enter the flat image.
    (out / "head.S").write_text(".text\n.global _start\n_start:\n.word 0x12345678\n",
                               encoding="utf-8")
    (out / "payload.c").write_text(
        "unsigned state = 7;\nunsigned zero;\nunsigned payload(void);\n"
        "unsigned payload(void) { return state + zero; }\n", encoding="utf-8")
    run([cc, "-c", "head.S", "-o", "head.o"], cwd=out)
    run([cc, "-fPIC", "-ffreestanding", "-fno-unwind-tables",
         "-fno-asynchronous-unwind-tables", "-c", "payload.c", "-o", "payload.o"], cwd=out)

    def layout(name, script, extra):
        # Omit --noinhibit-exec so a malformed ELF cannot pass by leaving output.
        run([ld, "--no-dynamic-linker", "-shared", "-Bsymbolic", "--no-undefined",
             *extra, "-T", str(src / script), "-o", f"{name}.elf",
             "head.o", "payload.o"], cwd=out)
        segments = elf_segments(out / f"{name}.elf")
        loads = [segment for segment in segments if segment[0] == 1]
        assert loads and loads[0][2] == 0, segments
        assert not any(segment[0] == 6 for segment in segments), segments
        if name == "efi":
            dynamic = [segment for segment in segments if segment[0] == 2]
            assert len(dynamic) == 1 and dynamic[0][4] > 0, segments
            d = dynamic[0]
            assert any(p[1] <= d[1] and d[1] + d[4] <= p[1] + p[4]
                       for p in loads), segments
        run([objcopy, "-O", "binary", f"{name}.elf", f"{name}.bin"], cwd=out)
        assert (out / f"{name}.bin").read_bytes()[:4] == b"\x78\x56\x34\x12"

    check("RISC OS module zero-based payload", lambda: layout(
        "module", "sys/arch/acorn32/stand/module.ldscript", []))
    check("ARM EFI payload and dynamic segment", lambda: layout(
        "efi", "sys/external/bsd/gnu-efi/dist/gnuefi/elf_arm_efi.lds", ["-nostdlib", "-N"]))

    def ioctl():
        (out / "machine").symlink_to(src / "sys/arch/acorn32/include")
        (out / "arm").symlink_to(src / "sys/arch/arm/include")
        flags = [f"--sysroot={args.sysroot.resolve()}", "-O2", "-ffreestanding",
                 "-fno-unwind-tables", "-fPIC", "-ffunction-sections", "-D_STANDALONE",
                 "-DLIBSA_SINGLE_DEVICE=rodisk_", "-DLIBSA_NO_RAW_ACCESS", "-DHEAP_VARIABLE",
                 "-I.", f"-I{src}/sys", f"-I{src}/sys/arch/acorn32/stand/lib",
                 "-Wall", "-Wstrict-prototypes", "-Wmissing-prototypes", "-Werror"]
        for source, obj in (("sys/arch/acorn32/stand/lib/riscosdisk.c", "riscosdisk.o"),
                            ("sys/lib/libsa/dev.c", "dev.o")):
            run([cc, *flags, "-c", str(src / source), "-o", obj], cwd=out)
        (out / "ioctl.c").write_text(
            "#include <lib/libsa/stand.h>\n#include <riscosdisk.h>\n"
            "int probe(void);\nint probe(void) { return DEV_IOCTL(0)(0, 0, 0); }\n",
            encoding="utf-8")
        run([cc, *flags, "-c", "ioctl.c", "-o", "ioctl.o"], cwd=out)
        run([ld, "--gc-sections", "--no-undefined", "-e", "probe", "-o", "ioctl.elf",
             "ioctl.o", "riscosdisk.o", "dev.o"], cwd=out)

    check("actual single-device ioctl resolves through libsa", ioctl)
    print(f"GCC {version}, {target}; object/link checks only, no firmware execution")
    return bool(failures)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        sys.exit(str(error))
