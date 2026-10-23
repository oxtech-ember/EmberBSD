# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Shared support for owning-libfdt host contracts.
"""Compile an OS FDT contract with the actual libfdt implementation."""
import os
import shlex
import subprocess

LIB_SOURCES = ("fdt.c", "fdt_ro.c", "fdt_rw.c", "fdt_wip.c", "fdt_sw.c",
               "fdt_empty_tree.c", "fdt_strerror.c")


def build_and_run(source, work, flags, arguments=()):
    lib = source / "sys/external/bsd/libfdt/dist"
    cc = shlex.split(os.environ.get("CC", "cc"))
    objects = []
    # Preserve upstream diagnostics. The new fixture has its own -Werror gate.
    for name in LIB_SOURCES:
        obj = work / (name + ".o")
        subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra"] + flags +
                       ["-I" + str(lib), "-c", str(lib / name), "-o", str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror"] + flags +
                   ["-I" + str(lib), str(work / "contract.c")] + objects +
                   ["-o", str(work / "contract")], check=True)
    subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                   [str(work / "contract"), *arguments], check=True, timeout=30)
