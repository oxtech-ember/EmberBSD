#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the production reset helpers and QUERY_STATE2 flag branch."""

import os
from pathlib import Path
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
uapi_path = root / "sys/external/bsd/drm2/dist/include/uapi/drm/amdgpu_drm.h"
header_path = amdgpu / "amdgpu.h"
ctx_path = amdgpu / "amdgpu_ctx.c"
header = header_path.read_text(encoding="utf-8")
uapi = uapi_path.read_text(encoding="utf-8")
ctx = ctx_path.read_text(encoding="utf-8")

helpers = []
for name in ("amdgpu_in_reset", "amdgpu_set_in_reset"):
    pattern = r"^static inline (?:bool|void)\n" + re.escape(name) + r"\([^\n]*\)\n\{.*?^\}"
    match = re.search(pattern, header, re.MULTILINE | re.DOTALL)
    if not match:
        fail(f"Missing production helper {name}")
    helpers.append(match.group(0))

flag = next(
    (line for line in uapi.splitlines(keepends=True)
     if re.match(r"^#define AMDGPU_CTX_QUERY2_FLAGS_RESET_IN_PROGRESS\s", line)),
    "",
)
if not flag:
    fail("Missing RESET_IN_PROGRESS UAPI flag")

query2_match = re.search(r"^static int amdgpu_ctx_query2\(", ctx, re.MULTILINE)
if not query2_match:
    fail("Missing amdgpu_ctx_query2")
end_match = re.search(r"^}", ctx[query2_match.start():], re.MULTILINE)
if not end_match:
    fail("Unclosed amdgpu_ctx_query2")
query2 = ctx[query2_match.start():query2_match.start() + end_match.end()]
branch_match = re.search(
    r"\tif \(amdgpu_in_reset\(adev\)\)\n"
    r"\t\tout->state\.flags \|= AMDGPU_CTX_QUERY2_FLAGS_RESET_IN_PROGRESS;",
    query2,
)
if not branch_match:
    fail("Missing QUERY_STATE2 reset-in-progress branch")

licenses = []
for source in (header, uapi, ctx):
    licenses.append(
        "\n".join(re.findall(r"/\*.*?\*/|//[^\n]*", source[:source.index("#include")], re.DOTALL))
    )

fixture_path = Path(__file__).with_name("amdgpu-reset-fixture.c")
code = fixture_path.read_text(encoding="utf-8")
parts = {
    "LICENSES": "\n".join(licenses),
    "UAPI_FLAG": flag,
    "HELPERS": "\n".join(helpers),
    "QUERY_BRANCH": branch_match.group(0),
}
for name, text in parts.items():
    marker = f"@{name}@"
    if marker not in code:
        fail(f"Missing fixture marker {marker}")
    code = code.replace(marker, text, 1)
if re.search(r"@[A-Z_]+@", code):
    fail("Unexpanded fixture marker")

cc = shlex.split(os.environ.get("HOST_CC", "cc"))
flags = shlex.split(
    os.environ.get(
        "HOST_CFLAGS",
        "-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined",
    )
)
with tempfile.TemporaryDirectory(prefix="ember-amdgpu-reset-") as work:
    source_path = Path(work) / "test.c"
    binary_path = Path(work) / "test"
    source_path.write_text(code, encoding="utf-8")
    build = subprocess.run(
        cc + flags + [str(source_path), "-o", str(binary_path)],
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
