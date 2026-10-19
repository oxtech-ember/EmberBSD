#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted; actual-source read-only FE contract.
"""Build and run the FE admission checks without hardware access."""
import os
import re
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("NPU_FE_SOURCE_ROOT", tools.parent.parent))
    with tempfile.TemporaryDirectory(prefix="a733-npu-fe-") as directory:
        work = Path(directory)
        for suffix in ("c", "h"):
            name = f"sun60i_a733_npu_fe.{suffix}"
            source = (root / "sys/arch/arm/sunxi" / name).read_text()
            text = "\n".join(line for line in source.splitlines()
                             if not line.startswith(("#include ", "__KERNEL_RCSID(")))
            (work / name).write_text(text + "\n")
        driver = (root / "sys/arch/arm/sunxi/sun60i_a733_npu.c").read_text()
        for name in ("sun60i_npu_mmio_ready", "sun60i_npu_engine_prepare",
                     "sun60i_npu_sysctl_engine_value"):
            match = re.search(r"^" + name + r"\(.*?^}", driver,
                              re.MULTILINE | re.DOTALL)
            if match is None:
                raise RuntimeError("missing production function: " + name)
            (work / (name + ".h")).write_text("static int\n" + match.group(0) + "\n")
        dma_header = (root / "sys/arch/arm/sunxi/sun60i_a733_npu_dma.h").read_text()
        (work / "sun60i_a733_npu_dma.h").write_text("\n".join(
            line for line in dma_header.splitlines() if not line.startswith("#include ")) + "\n")
        fe = (root / "sys/arch/arm/sunxi/sun60i_a733_npu_fe.c").read_text()
        pure = []
        for name in ("sun60i_a733_npu_fe_admit", "sun60i_a733_npu_fe_event_image"):
            pure.append("int\n" + re.search(r"^" + name + r"\(.*?^}", fe,
                        re.MULTILINE | re.DOTALL).group(0) + "\n")
        (work / "npu-fe-pure.h").write_text("\n".join(pure))
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("NPU_FE_CFLAGS", "")) +
                   [f"-I{work}", str(tools / "a733-npu-fe-contract.c"),
                    "-o", str(work / "test")])
        for fixture in ("a733-npu-fe-contract", "a733-npu-engine-contract"):
            argv = list(command)
            argv[argv.index(str(tools / "a733-npu-fe-contract.c"))] = str(tools / (fixture + ".c"))
            subprocess.run(argv, check=True)
            subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                           [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
