#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted; execute the production ownership core.
"""Host contract. This test does not establish hardware display readiness."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    source = Path(os.environ.get("DISPLAY_OWNER_SOURCE_ROOT", tools.parent.parent))
    driver = source / "sys/arch/arm/sunxi"
    with tempfile.TemporaryDirectory(prefix="a733-display-owner-") as directory:
        work = Path(directory)
        for name in ("sun60i_a733_display", "sun60i_a733_display_owner"):
            for extension in ("h", "c"):
                text = (driver / f"{name}.{extension}").read_text()
                # Keep every function body. Only replace kernel include plumbing.
                text = "\n".join(line for line in text.splitlines()
                                 if not line.startswith(("#include ", "__KERNEL_RCSID(")))
                (work / f"{name}.{extension}").write_text(text + "\n")
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("DISPLAY_OWNER_CFLAGS", "")) +
                   [f"-I{work}", str(tools / "a733-display-owner-contract.c"),
                    "-o", str(work / "test")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
