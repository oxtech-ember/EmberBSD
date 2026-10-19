#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted.
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("A733_TV0_SOURCE_ROOT", tools.parent.parent))
    source = root / "sys/arch/arm/sunxi"
    with tempfile.TemporaryDirectory(prefix="a733-tv0-") as directory:
        work = Path(directory)
        headers = work / "arm/sunxi"
        headers.mkdir(parents=True)
        for name in ("sun60i_a733_output", "sun60i_a733_tv0"):
            (headers / (name + ".h")).write_bytes((source / (name + ".h")).read_bytes())
            lines = (source / (name + ".c")).read_text().splitlines(keepends=True)
            # Keep each function body unchanged. Replace kernel includes only.
            body = "".join(line for line in lines
                           if not line.startswith("#include <sys/")
                           and not line.startswith("__KERNEL_RCSID"))
            (work / (name + ".c")).write_text(body)
        binary = work / "contract"
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("A733_TV0_CFLAGS", "")) +
                   ["-I" + str(work), str(tools / "a733-tv0-contract.c"),
                    "-o", str(binary)])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(binary)], check=True)


if __name__ == "__main__":
    main()
