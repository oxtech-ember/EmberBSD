#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted actual-source NPU initialization contract.
"""Exercise the real stage dispatcher and tile ioctl with mocked hardware."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("NPU_INIT_SOURCE_ROOT", tools.parent.parent))
    driver = root / "sys/arch/arm/sunxi/sun60i_a733_npu.c"
    source = driver.read_text()
    with tempfile.TemporaryDirectory(prefix="a733-npu-init-") as directory:
        work = Path(directory)
        start = source.index("static int\nsun60i_npu_run_stage(")
        end = source.index("/* Caller holds sc_run_lock.", start)
        ioctl = source.index("static int\nsun60i_npu_tile_ioctl(")
        after_ioctl = source.index("/*", ioctl)
        (work / "npu-init.h").write_text(source[start:end] +
                                        source[ioctl:after_ioctl])
        argv = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror",
        ] + shlex.split(os.environ.get("NPU_INIT_CFLAGS", "")) + [
            "-I" + str(work),
            str(tools / "a733-npu-init-contract.c"), "-o", str(work / "test"),
        ]
        subprocess.run(argv, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
