#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted; actual-source native run contract.
"""Run bounded native NPU engine controls without hardware."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("NPU_RUN_SOURCE_ROOT", tools.parent.parent))
    with tempfile.TemporaryDirectory(prefix="a733-npu-run-") as directory:
        work = Path(directory)
        for stem in ("sun60i_a733_npu_run", "sun60i_a733_npu_fe"):
            for suffix in ("c", "h"):
                name = f"{stem}.{suffix}"
                body = (root / "sys/arch/arm/sunxi" / name).read_text()
                (work / name).write_text("\n".join(line for line in body.splitlines()
                    if not line.startswith(("#include ", "__KERNEL_RCSID("))) + "\n")
        argv = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror"]
        argv += shlex.split(os.environ.get("NPU_RUN_CFLAGS", ""))
        argv += ["-I" + str(work), str(tools / "a733-npu-run-contract.c"),
                 "-o", str(work / "test")]
        subprocess.run(argv, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
