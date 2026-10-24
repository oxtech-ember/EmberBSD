#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Reject i915 compute UABI exposure without default-context engine mapping."""

from pathlib import Path
import re
import sys


def function_body(source, signature):
    # Some kernel functions put a preprocessor definition between the
    # signature and opening brace (for example LOOKUP_USER_INDEX).
    match = re.search(
        signature + r"\s*\([^)]*\)(?:\s*#define[^\n]*\n)?\s*\{(.*?)^\}",
        source,
        re.M | re.S,
    )
    if match is None:
        raise RuntimeError(f"missing function matching {signature}")
    return match.group(1)


def check(root):
    user_path = root / "sys/external/bsd/drm2/dist/drm/i915/gt/intel_engine_user.c"
    context_path = root / "sys/external/bsd/drm2/dist/drm/i915/gem/i915_gem_context.c"
    user = user_path.read_text(encoding="utf-8")
    context = context_path.read_text(encoding="utf-8")

    user_array = re.search(
        r"static\s+const\s+u8\s+uabi_classes\s*(?:\[[^]]*\])?\s*=\s*\{(.*?)\};",
        user,
        re.S,
    )
    if user_array is None:
        raise RuntimeError("missing UABI engine-class map")
    compute_is_published = re.search(
        r"\[\s*COMPUTE_CLASS\s*\]\s*=\s*I915_ENGINE_CLASS_COMPUTE",
        user_array.group(1),
    ) is not None

    add_user = function_body(user, r"void\s+intel_engine_add_user")
    compute_is_registered = not re.search(
        r"if\s*\([^)]*COMPUTE_CLASS[^)]*\)\s*(?:\{\s*)?return\s*;",
        add_user,
        re.S,
    )

    legacy = function_body(user, r"static\s+int\s+legacy_ring_idx")
    compute_is_rejected_by_legacy_mapping = re.search(
        r"ring->class\s*==\s*COMPUTE_CLASS[^;]*return\s+INVALID_ENGINE\s*;",
        legacy,
        re.S,
    ) is not None

    default_context = function_body(context, r"lookup_user_engine")
    uses_legacy_index = re.search(r"idx\s*=\s*engine->legacy_idx\s*;", default_context) is not None
    if (
        compute_is_published
        and compute_is_registered
        and uses_legacy_index
        and compute_is_rejected_by_legacy_mapping
    ):
        raise RuntimeError(
            "CCS is queryable but rejected by default-context legacy engine selection"
        )


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: i915-engine-uabi-contract.py SOURCE_ROOT")
    try:
        check(Path(sys.argv[1]))
    except (OSError, RuntimeError) as error:
        raise SystemExit(f"i915 engine UABI contract: {error}")
    print("i915 engine UABI contract: passed")


if __name__ == "__main__":
    main()
