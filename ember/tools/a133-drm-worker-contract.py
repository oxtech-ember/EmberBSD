#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted.
"""Build the production A133 worker against host DRM and hardware doubles."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

tools = Path(__file__).resolve().parent
root = tools.parents[1]
worker = (root / "sys/arch/arm/sunxi/sun50i_a133_drm.c").read_text()
start = worker.index("static void\na133_commit_work")
end = worker.index("static int\na133_atomic_commit", start)
with tempfile.TemporaryDirectory(prefix="a133-drm-worker-") as tmp:
    output = Path(tmp)
    (output / "worker.c").write_text(worker[start:end])
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) +
        ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
        shlex.split(os.environ.get("A733_DRM_CFLAGS", "")) +
        ["-I" + tmp, str(tools / "a133-drm-worker-contract.c"),
         "-o", str(output / "contract")], check=True)
    subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
        [str(output / "contract")], check=True)
