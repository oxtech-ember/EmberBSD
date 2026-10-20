#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted; actual-source fixed NN image contract.
"""Check the finite CPU image without DMA or hardware."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("NPU_NN_SOURCE_ROOT", tools.parent.parent))
    with tempfile.TemporaryDirectory(prefix="a733-npu-nn-") as directory:
        work = Path(directory)
        for suffix in ("c", "h"):
            name = f"sun60i_a733_npu_nn.{suffix}"
            body = (root / "sys/arch/arm/sunxi" / name).read_text()
            (work / name).write_text("\n".join(line for line in body.splitlines()
                if not line.startswith(("#include ", "__KERNEL_RCSID("))) + "\n")
        argv = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror"]
        argv += shlex.split(os.environ.get("NPU_NN_CFLAGS", ""))
        argv += ["-I" + str(work), str(tools / "a733-npu-nn-contract.c"),
                 "-o", str(work / "test")]
        subprocess.run(argv, check=True)
        test = shlex.split(os.environ.get("TEST_RUNNER", "")) + [str(work / "test")]
        subprocess.run(test, check=True)
        image = subprocess.check_output(test + ["--image"])
        if len(image) != 1632 or hashlib.sha256(image[1120:]).hexdigest() != \
                "6b14aa6b1aba47323ba262ae69cba4dd3a970d172097e8d5730736b6bfaafedd":
            raise RuntimeError("The independent expected-image SHA256 differs")
        print("A733 NN independent expected-image SHA256 passed")


if __name__ == "__main__":
    main()
