#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Production shared KMS flip fence and event contract.
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
        default=tools.parents[1] / "sys/external/bsd/drm2/drm/ember_drm_flip.c")
    args = parser.parse_args()
    text = args.source.read_text()
    start = text.index("void\nember_drm_flip_finish")
    with tempfile.TemporaryDirectory(prefix="drmkms-flip-") as tmp:
        work = Path(tmp)
        (work / "event.c").write_text(text[start:])
        command = (shlex.split(os.environ.get("CC", "cc")) +
            ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
            shlex.split(os.environ.get("A733_DRM_CFLAGS", "")) +
            ["-I" + str(work), str(tools / "a733-drm-event-contract.c"),
             "-o", str(work / "contract")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "contract")], check=True)


if __name__ == "__main__":
    main()
