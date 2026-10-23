#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted. Execute the actual startup scheduler.
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    source = tools.parent.parent / "sys/arch/arm/sunxi/sun60i_a733_drm.c"
    text = source.read_text()
    start = text.index("static int\na733_start(")
    end = text.index("\nstatic int\na733_fault(", start)
    with tempfile.TemporaryDirectory(prefix="a733-drm-startup-") as directory:
        work = Path(directory)
        (work / "startup.c").write_text(
            text[:text.index("#include")] + text[start:end])
        command = (shlex.split(os.environ.get("CC", "gcc-16")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("A733_DRM_CFLAGS", "")) +
                   ["-I" + str(work), str(tools / "a733-drm-startup-contract.c"),
                    "-o", str(work / "contract")])
        subprocess.run(command, check=True)
        subprocess.run([str(work / "contract")], check=True)


if __name__ == "__main__":
    main()
