#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Native-UAPI KMS consumer contract.
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True


def main():
    tools = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        default=tools / "a733-drm-check.c")
    parser.add_argument("--uapi", type=Path,
                        default=tools.parents[1] / "sys/external/bsd/drm2/dist/include/uapi")
    parser.add_argument("--scenario", type=int, choices=range(40))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="a733-drm-check-") as directory:
        work = Path(directory)
        shutil.copyfile(args.source, work / "a733-drm-check.c")
        shutil.copyfile(tools / "a733-drm-check-contract.c", work / "contract.c")
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("A733_DRM_CFLAGS", "")) +
                   ["-I" + str(args.uapi), str(work / "contract.c"),
                    "-o", str(work / "contract")])
        subprocess.run(command, check=True)
        arguments = [] if args.scenario is None else [str(args.scenario)]
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "contract")] + arguments, check=True)


if __name__ == "__main__":
    main()
