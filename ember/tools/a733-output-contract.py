#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted. Execute the production timing core.
"""Check A733 timing fields without accessing display hardware."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def main():
    tools = Path(__file__).resolve().parent
    source = Path(os.environ.get("OUTPUT_SOURCE_ROOT", tools.parent.parent))
    driver = source / "sys/arch/arm/sunxi"
    with tempfile.TemporaryDirectory(prefix="a733-output-") as directory:
        work = Path(directory)
        for name in ("sun60i_a733_output.h", "sun60i_a733_output.c",
                     "sun60i_a733_output_provider.c", "sun60i_a733_hdmi_phy.h"):
            text = (driver / name).read_text()
            # Preserve every function body. Replace only kernel include plumbing.
            text = "\n".join(line for line in text.splitlines()
                             if not line.startswith(("#include ", "__KERNEL_RCSID(")))
            (work / name).write_text(text + "\n")
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("OUTPUT_CFLAGS", "")) +
                   [f"-I{work}", str(tools / "a733-output-contract.c"),
                    "-o", str(work / "test")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
