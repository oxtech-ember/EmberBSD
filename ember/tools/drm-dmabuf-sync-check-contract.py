#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted. Actual probe with controlled syscall failures.
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    root = tools.parent.parent
    source = (tools / "drm-dmabuf-sync-check.c").read_text()
    with tempfile.TemporaryDirectory(prefix="dmabuf-sync-check-") as directory:
        work = Path(directory)
        (work / "probe-body.h").write_text("\n".join(
            line for line in source.splitlines() if not line.startswith("#include ")) + "\n")
        abi = (root / "sys/sys/dmabuf.h").read_text()
        (work / "dmabuf-abi.h").write_text("\n".join(
            line for line in abi.splitlines() if not line.startswith("#include ")) + "\n")
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        command += shlex.split(os.environ.get("DRM_CFLAGS", ""))
        command += ["-I" + str(work),
                    "-I" + str(root / "sys/external/bsd/drm2/powervr"),
                    "-I" + str(root / "sys/external/bsd/drm2/dist/include/uapi"),
                    str(tools / "drm-dmabuf-sync-check-contract.c"),
                    "-o", str(work / "check")]
        subprocess.run(command, check=True)
        subprocess.run([str(work / "check")], check=True)


if __name__ == "__main__":
    main()
