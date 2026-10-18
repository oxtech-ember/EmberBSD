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
    root = Path(os.environ.get("A733_HDMI_SOURCE_ROOT", tools.parent.parent))
    source = root / "sys/arch/arm/sunxi"
    with tempfile.TemporaryDirectory(prefix="a733-hdmi-phy-") as directory:
        binary = Path(directory) / "contract"
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("A733_HDMI_CFLAGS", "")) +
                   ["-I" + str(source), str(tools / "a733-hdmi-phy-contract.c"),
                    str(source / "sun60i_a733_hdmi_phy.c"), "-o", str(binary)])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(binary)], check=True)


if __name__ == "__main__":
    main()
