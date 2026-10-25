#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the production stable-pstate UAPI validator with boundary checks."""

from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile


def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(1)


root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
amdgpu = root / "sys/external/bsd/drm2/dist/drm/amd/amdgpu"
uapi_dir = root / "sys/external/bsd/drm2/dist/include/uapi/drm"
ctx = (amdgpu / "amdgpu_ctx.c").read_text(encoding="utf-8")

match = re.search(
    r"^static int amdgpu_ctx_stable_pstate_flags\([^\n]*\n"
    r"(?:[^\n]*\n)*?\{.*?^\}",
    ctx,
    re.MULTILINE | re.DOTALL,
)
if not match:
    fail("Missing production stable-pstate UAPI validator")

fixture = Path(__file__).with_name("amdgpu-stable-pstate-fixture.c")
code = fixture.read_text(encoding="utf-8")
for marker, replacement in (
    ("@UAPI_HEADER@", f'#include "{uapi_dir / "amdgpu_drm.h"}"'),
    ("@VALIDATOR@", match.group(0)),
):
    if marker not in code:
        fail(f"Missing fixture marker {marker}")
    code = code.replace(marker, replacement, 1)
if re.search(r"@[A-Z_]+@", code):
    fail("Unexpanded fixture marker")

cc = shlex.split(os.environ.get("HOST_CC", "cc"))
flags = shlex.split(
    os.environ.get(
        "HOST_CFLAGS",
        "-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined",
    )
)
with tempfile.TemporaryDirectory(prefix="ember-amdgpu-stable-pstate-") as work:
    source_path = Path(work) / "test.c"
    binary_path = Path(work) / "test"
    source_path.write_text(code, encoding="utf-8")
    build = subprocess.run(
        cc + flags + ["-I", str(uapi_dir), str(source_path), "-o", str(binary_path)],
        capture_output=True,
        text=True,
    )
    if build.returncode:
        sys.stderr.write(build.stdout + build.stderr)
        raise SystemExit(build.returncode)
    result = subprocess.run([str(binary_path)], capture_output=True, text=True)
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(result.returncode)
    sys.stdout.write(result.stdout)
