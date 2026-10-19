#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Run the actual KMS worker contract.
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True


def main():
    tools = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
        default=tools.parents[1] / "sys/arch/arm/sunxi/sun60i_a733_drm_atomic.c")
    args = parser.parse_args()
    text = args.source.read_text()
    text = text[text.index("static int\na733_explicit_fence"):]
    with tempfile.TemporaryDirectory(prefix="a733-drm-worker-") as tmp:
        work = Path(tmp)
        (work / "worker.c").write_text(text)
        command = (shlex.split(os.environ.get("CC", "cc")) +
            ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
            shlex.split(os.environ.get("A733_DRM_CFLAGS", "")) +
            ["-I" + str(work), str(tools / "a733-drm-worker-contract.c"),
             "-o", str(work / "contract")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "contract")], check=True)


if __name__ == "__main__":
    main()
