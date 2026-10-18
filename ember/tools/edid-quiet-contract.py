#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted. Execute the unchanged parser bodies.
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("EDID_SOURCE_ROOT", tools.parent.parent))
    source = root / "sys/dev/videomode"
    with tempfile.TemporaryDirectory(prefix="edid-quiet-") as directory:
        work = Path(directory)
        headers = work / "dev/videomode"
        headers.mkdir(parents=True)
        for name in ("videomode.h", "vesagtf.h", "edidvar.h", "edidreg.h",
                     "ediddevs.h", "ediddevs_data.h"):
            (headers / name).write_bytes((source / name).read_bytes())
        for name in ("edid.c", "videomode.c", "vesagtf.c"):
            # Replace kernel include plumbing. Keep every function body.
            text = (source / name).read_text()
            text = "\n".join(line for line in text.splitlines()
                             if not line.startswith(("#include <sys/", "__KERNEL_RCSID(")))
            (work / name).write_text(text + "\n")
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Werror"] +
                   shlex.split(os.environ.get("EDID_CFLAGS", "")) +
                   ["-I" + str(work), str(tools / "edid-quiet-contract.c"),
                    "-o", str(work / "test")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
