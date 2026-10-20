#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted; actual-source fixed TASK simulation controls.
"""Check native TASK lifetime and terminal proof without hardware."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    source = Path(os.environ.get("NPU_TASK_SOURCE_ROOT", tools.parent.parent))
    with tempfile.TemporaryDirectory(prefix="a733-npu-task-") as directory:
        work = Path(directory)
        for stem in ("mmu", "nn", "dma", "fe", "run", "task"):
            for suffix in ("c", "h"):
                name = f"sun60i_a733_npu_{stem}.{suffix}"
                body = (source / "sys/arch/arm/sunxi" / name).read_text()
                (work / name).write_text("\n".join(line for line in body.splitlines()
                    if not line.startswith(("#include ", "__KERNEL_RCSID("))) + "\n")
        argv = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror"]
        argv += shlex.split(os.environ.get("NPU_TASK_CFLAGS", ""))
        argv += ["-I" + str(work), str(tools / "a733-npu-task-contract.c"),
                 "-o", str(work / "test")]
        subprocess.run(argv, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
