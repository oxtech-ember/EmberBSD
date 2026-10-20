#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted; actual-source CPU-only DMA contract.
"""Build and run the DMA lifetime checks without hardware access."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("NPU_DMA_SOURCE_ROOT", tools.parent.parent))
    with tempfile.TemporaryDirectory(prefix="a733-npu-dma-") as directory:
        work = Path(directory)
        for stem in ("sun60i_a733_npu_mmu", "sun60i_a733_npu_dma",
                     "sun60i_a733_npu_nn", "sun60i_a733_npu_run"):
            for suffix in ("c", "h"):
                name = f"{stem}.{suffix}"
                source = (root / "sys/arch/arm/sunxi" / name).read_text()
                text = "\n".join(line for line in source.splitlines()
                                 if not line.startswith(("#include ", "__KERNEL_RCSID(")))
                (work / name).write_text(text + "\n")
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("NPU_DMA_CFLAGS", "")) +
                   [f"-I{work}", str(tools / "a733-npu-dma-contract.c"),
                    "-o", str(work / "test")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
