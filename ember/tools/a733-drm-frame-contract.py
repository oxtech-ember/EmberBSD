#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Actual native DRM frame bridge contract.
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def body(path):
    return "".join(line for line in path.read_text().splitlines(keepends=True)
                   if not line.startswith("#include ")
                   and not line.startswith("__KERNEL_RCSID"))


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("A733_DRM_SOURCE_ROOT", tools.parent.parent))
    source = root / "sys/arch/arm/sunxi"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scanout-header", type=Path,
                        default=source / "sun60i_a733_de352_scanout.h")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="a733-drm-frame-") as directory:
        work = Path(directory)
        for name in ("sun60i_a733_output.h", "sun60i_a733_drm_frame.h",
                     "sun60i_a733_drm_frame.c"):
            (work / name).write_text(body(source / name))
        (work / "sun60i_a733_de352_scanout.h").write_text(body(args.scanout_header))
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("A733_DRM_CFLAGS", "")) +
                   ["-I" + str(work), str(tools / "a733-drm-frame-contract.c"),
                    "-o", str(work / "contract")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "contract")], check=True)


if __name__ == "__main__":
    main()
