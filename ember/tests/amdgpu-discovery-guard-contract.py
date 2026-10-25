#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Execute the production IP-discovery guard with a controlled helper."""

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


def extract_block(source, start):
    opening = source.find("{", start)
    if opening < 0:
        fail("Missing production discovery block")
    depth = 0
    for offset in range(opening, len(source)):
        if source[offset] == "{":
            depth += 1
        elif source[offset] == "}":
            depth -= 1
            if depth == 0:
                return source[start : offset + 1]
    fail("Unclosed production discovery block")


def source_comments(source):
    include = source.find("#include")
    if include < 0:
        fail("Missing include boundary in amdgpu_kms.c")
    prefix = source[:include]
    comments = re.findall(r"/\*.*?\*/|//[^\n]*(?:\n|$)", prefix, re.DOTALL)
    if not comments or not any("Permission is hereby granted" in item for item in comments):
        fail("Missing upstream license comments in amdgpu_kms.c prefix")
    return "\n".join(item.rstrip() for item in comments)


root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
amdgpu = root / "sys/external/bsd/drm2/dist/drm/amd/amdgpu"
kms_source = (amdgpu / "amdgpu_kms.c").read_text(encoding="utf-8")
if not re.search(r"uint32_t ip_discovery_version\s*=\s*0;", kms_source):
    fail("IP discovery version must default to zero")
if "result->ip_discovery_version = ip_discovery_version;" not in kms_source:
    fail("IP discovery version fallback must reach the UAPI result")

condition = "if (adev->discovery && adev->asic_type >= CHIP_VEGA10)"
start = kms_source.find(condition)
if start < 0:
    fail("Missing production discovery pointer and ASIC guard")
branch = extract_block(kms_source, start)
comments = source_comments(kms_source)

template = Path(__file__).with_name("amdgpu-discovery-guard-fixture.c").read_text(
    encoding="utf-8"
)
for marker in ("@SOURCE_COMMENTS@", "@DISCOVERY_BRANCH@"):
    if marker not in template:
        fail(f"Missing fixture marker: {marker}")


def make_fixture(discovery_branch):
    fixture_text = template.replace("@SOURCE_COMMENTS@", comments, 1)
    fixture_text = fixture_text.replace("@DISCOVERY_BRANCH@", discovery_branch, 1)
    if re.search(r"@[A-Z_]+@", fixture_text):
        fail("Unexpanded fixture marker")
    return fixture_text


fixture = make_fixture(branch)
mutant_branch = branch.replace("adev->discovery && ", "", 1)
if mutant_branch == branch:
    fail("Could not create null-discovery causal mutant")
mutant_fixture = make_fixture(mutant_branch)

cc = shlex.split(os.environ.get("HOST_CC", "cc"))
flags = shlex.split(
    os.environ.get(
        "HOST_CFLAGS",
        "-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined",
    )
)
with tempfile.TemporaryDirectory(prefix="ember-amdgpu-discovery-guard-") as directory:
    work = Path(directory)

    def compile_fixture(name, source):
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
            raise SystemExit(build.returncode)
        return binary_path

    production_binary = compile_fixture("production", fixture)
    result = subprocess.run(
        [str(production_binary)], capture_output=True, text=True
    )
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(result.returncode)
    if "AMDGPU discovery guard execution contract: PASS" not in result.stdout:
        fail("Missing discovery guard PASS marker")
    sys.stdout.write(result.stdout)

    mutant_binary = compile_fixture("mutant", mutant_fixture)
    mutant = subprocess.run([str(mutant_binary)], capture_output=True, text=True)
    expected_failure = "FAIL: NULL discovery: fallback version must remain zero"
    if mutant.returncode != 1 or expected_failure not in mutant.stderr:
        sys.stderr.write(mutant.stdout + mutant.stderr)
        fail("Null-discovery mutant did not fail with the expected invariant")
    print("AMDGPU discovery guard causal mutant: PASS (compiled; expected exit 1)")
