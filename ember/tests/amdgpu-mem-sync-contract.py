#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Execute the production SA-allocated IB path and a causal mutation."""

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


def extract_function(source, name):
    match = re.search(
        r"(?m)^[\w\s*]+\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{",
        source,
    )
    if not match:
        fail(f"Missing production function {name}")
    start = source.find("{", match.start())
    return source[match.start() : _block_end(source, start)]


def _block_end(source, start):
    if start < 0 or source[start] != "{":
        fail("Missing production function body")
    depth = 0
    for offset in range(start, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return offset + 1
    fail("Unclosed production function body")


def render_fixture(template, replacements):
    for marker, value in replacements.items():
        token = f"@{marker}@"
        if token not in template:
            fail(f"Missing fixture marker {token}")
        template = template.replace(token, value, 1)
    if re.search(r"@[A-Z_]+@", template):
        fail("Unexpanded fixture marker")
    return template


def compile_and_run(cc, flags, work, name, source, expected):
    source_path = work / f"{name}.c"
    binary_path = work / name
    source_path.write_text(source, encoding="utf-8")
    build = subprocess.run(
        cc + flags + [str(source_path), "-o", str(binary_path)],
        capture_output=True,
        text=True,
    )
    if build.returncode:
        sys.stderr.write(build.stdout + build.stderr)
        fail(f"{name}: compilation failed")

    result = subprocess.run([str(binary_path)], capture_output=True, text=True)
    if expected == "pass":
        if result.returncode:
            sys.stderr.write(result.stdout + result.stderr)
            fail(f"{name}: production execution failed")
        if "AMDGPU SA-allocated IB execution contract: PASS" not in result.stdout:
            fail(f"{name}: missing production PASS marker")
        return result.stdout

    causal_failure = "FAIL: vm=NULL: expected EMIT_MEM_SYNC bit"
    if result.returncode != 1 or causal_failure not in result.stderr:
        sys.stderr.write(result.stdout + result.stderr)
        fail(f"{name}: mutant did not fail at the independent mem-sync check")
    return f"{name}: compilation PASS; expected causal execution failure confirmed"


root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
amdgpu = root / "sys/external/bsd/drm2/dist/drm/amd/amdgpu"
uapi_dir = root / "sys/external/bsd/drm2/dist/include/uapi/drm"
ib_source = (amdgpu / "amdgpu_ib.c").read_text(encoding="utf-8")
cs_source = (amdgpu / "amdgpu_cs.c").read_text(encoding="utf-8")
gfx_source = (amdgpu / "amdgpu_gfx_v9_0.c").read_text(encoding="utf-8")
uapi_source = (uapi_dir / "amdgpu_drm.h").read_text(encoding="utf-8")

uapi_match = re.search(
    r"^#define AMDGPU_IB_FLAG_EMIT_MEM_SYNC\s+\(1\s*<<\s*6\)\s*$",
    uapi_source,
    re.MULTILINE,
)
if not uapi_match:
    fail("Missing UAPI EMIT_MEM_SYNC bit 6")
uapi_flag = uapi_match.group(0)

ib_get = extract_function(ib_source, "amdgpu_ib_get")
flag_assignment = "ib->flags = AMDGPU_IB_FLAG_EMIT_MEM_SYNC;"
if ib_get.count(flag_assignment) != 1:
    fail("Expected one SA IB mem-sync default assignment in amdgpu_ib_get")

ib_schedule = extract_function(ib_source, "amdgpu_ib_schedule")
if not re.search(
    r"if\s*\(\s*\(ib->flags\s*&\s*AMDGPU_IB_FLAG_EMIT_MEM_SYNC\)\s*&&"
    r"\s*ring->funcs->emit_mem_sync\s*\)\s*"
    r"ring->funcs->emit_mem_sync\(ring\)\s*;",
    ib_schedule,
):
    fail("IB scheduling must invoke the ring hook when the flag is set")

cs_fill = extract_function(cs_source, "amdgpu_cs_ib_fill")
if not re.search(r"ib->flags\s*=\s*chunk_ib->flags\s*;", cs_fill):
    fail("User CS flags must explicitly replace the SA default")

mem_sync = extract_function(gfx_source, "gfx_v9_0_emit_mem_sync")
if "PACKET3(PACKET3_ACQUIRE_MEM, 5)" not in mem_sync:
    fail("GFX9 mem-sync hook must emit ACQUIRE_MEM")
if gfx_source.count(".emit_mem_sync = gfx_v9_0_emit_mem_sync") != 2:
    fail("GFX9 graphics and compute rings must register the mem-sync hook")
if gfx_source.count("7, /* gfx_v9_0_emit_mem_sync */") != 2:
    fail("GFX9 ring frame budgets must include the seven-dword hook")

def source_license(source):
    prefix = source[: source.index("#include")]
    return "\n".join(re.findall(r"/\*.*?\*/|//[^\n]*", prefix, re.DOTALL))


licenses = "\n\n".join((source_license(ib_source), source_license(uapi_source)))
template = Path(__file__).with_name("amdgpu-mem-sync-fixture.c").read_text(
    encoding="utf-8"
)
fixture = render_fixture(
    template,
    {"LICENSES": licenses, "UAPI_FLAG": uapi_flag, "IB_GET": ib_get},
)
removed = render_fixture(
    template,
    {
        "LICENSES": licenses,
        "UAPI_FLAG": uapi_flag,
        "IB_GET": ib_get.replace(flag_assignment, "/* mutation: default removed */"),
    },
)
zeroed = render_fixture(
    template,
    {
        "LICENSES": licenses,
        "UAPI_FLAG": uapi_flag,
        "IB_GET": ib_get.replace(flag_assignment, "ib->flags = 0;"),
    },
)

cc = shlex.split(os.environ.get("HOST_CC", "cc"))
flags = shlex.split(
    os.environ.get(
        "HOST_CFLAGS",
        "-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined",
    )
)
with tempfile.TemporaryDirectory(prefix="ember-amdgpu-mem-sync-") as directory:
    work = Path(directory)
    sys.stdout.write(compile_and_run(cc, flags, work, "production", fixture, "pass"))
    print(compile_and_run(cc, flags, work, "removed", removed, "mutant"))
    print(compile_and_run(cc, flags, work, "zeroed", zeroed, "mutant"))
