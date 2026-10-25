#!/usr/bin/env python3
# Copyright (c) 2026 EmberBSD contributors.
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD source-routing regression.
"""Check libelf source selection in BSD make without building targets."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[4]
ROUTER = ROOT / "external/bsd/elftoolchain/libelf-source.mk"
INPUTS = (
    "common/_elftc.h",
    "common/elfdefinitions.h",
    "common/sys/elfdefinitions.m4",
    "common/sys/elfconstants.m4",
    "libelf/elf.c",
    "libelf/libelf.h",
    "libelf/gelf.h",
    "libelf/elf_types.m4",
    "libelf/libelf_convert.m4",
    "ports.distinfo",
    "libelf-source.json",
)
STUB_INCLUDES = (
    "bsd.own.mk",
    "bsd.hostinit.mk",
    "bsd.inc.mk",
    "bsd.subdir.mk",
    "bsd.obj.mk",
    "bsd.lib.mk",
    "bsd.hostlib.mk",
)


CONSUMERS = (
    ("external/bsd/elftoolchain/common/Makefile", "external", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "SRCDIR": "common",
    }),
    ("external/bsd/elftoolchain/common/sys/Makefile", "external", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "SRCDIR": "common/sys",
    }),
    ("external/bsd/elftoolchain/lib/libelf/Makefile", "external", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "SRCDIR": "libelf",
        "SHLIB_MAJOR": "2",
        "SHLIB_MINOR": "0",
    }),
    ("external/bsd/elftoolchain/lib/libdwarf/Makefile", "external", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "SRCDIR": "dist/libdwarf",
        "CPPFLAGS": None,
    }),
    ("tools/elftoolchain/common/Makefile", "host", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "SRCDIR": "common",
    }),
    ("tools/elftoolchain/common/sys/Makefile", "host", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "SRCDIR": "common/sys",
    }),
    ("tools/elftoolchain/libelf/Makefile", "host", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "LIBELF_DIR": "libelf",
    }),
    ("tools/elftoolchain/libdwarf/Makefile", "host", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "LIBELF_DIR": "libelf",
        "COMMON_DIR": "common",
        "LIBDWARF_DIR": "dist/libdwarf",
    }),
    ("tools/Makefile.ctftools", "ctftools", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "LIBELF_DIR": "libelf",
        "LIBDWARF_DIR": "dist/libdwarf",
    }),
    ("tools/libctf/Makefile", "host", {
        "EMBER_LIBELF_SOURCE": "prepared",
        "LIBELF_DIR": "libelf",
    }),
)


def write_fixture(root):
    root.mkdir(parents=True, exist_ok=True)
    for relative in INPUTS:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("fixture\n", encoding="ascii")


def write_make_support(temp):
    mk = temp / "mk"
    mk.mkdir()
    for include in STUB_INCLUDES:
        (mk / include).write_text("", encoding="ascii")
    query = temp / "query.mk"
    return mk, query


def query_make(make, mk, query, makefile, cwd, variables):
    keys = tuple(variables)
    query.write_text(
        ".PHONY: __libelf_route_query\n"
        "__libelf_route_query:\n"
        + "".join(f".info ROUTE|{key}=${{{key}}}\n" for key in keys),
        encoding="ascii",
    )
    argv = [
        str(make), "-r", "-m", str(mk), "-f", str(makefile),
        "-f", str(query), "-n", "__libelf_route_query",
        "NOCOMPATLIB=yes", "PRINTOBJDIR=pwd", "HOST_CC=:",
    ]
    result = subprocess.run(argv, cwd=cwd, text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"BSD make query failed for {makefile}:\n"
            f"command: {argv!r}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    values = {}
    for key in keys:
        marker = re.compile(r"ROUTE\|" + re.escape(key) + r"=(.*)$", re.MULTILINE)
        matches = marker.findall(result.stderr)
        if len(matches) != 1:
            raise RuntimeError(
                f"query for {key} in {makefile} returned {len(matches)} values; "
                f"stderr:\n{result.stderr}"
            )
        values[key] = matches[0]
    return values, result.stderr


def require_equal(makefile, key, actual, expected):
    if Path(actual).resolve() != Path(expected).resolve():
        raise RuntimeError(
            f"{makefile}: {key} selected {actual!r}; expected {expected!r}"
        )


def require_value(makefile, key, actual, expected):
    if actual != expected:
        raise RuntimeError(
            f"{makefile}: {key} selected {actual!r}; expected {expected!r}"
        )


def check_router_modes(make, temp, mk, query):
    fixture_checkout = temp / "default-checkout"
    router_copy = fixture_checkout / "external/bsd/elftoolchain/libelf-source.mk"
    router_copy.parent.mkdir(parents=True)
    router_copy.write_bytes(ROUTER.read_bytes())
    default_source = router_copy.parent / "libelf-source"
    write_fixture(default_source)

    query.write_text(
        ".PHONY: __libelf_route_query\n"
        "__libelf_route_query:\n"
        ".info ROUTE|EMBER_LIBELF_SOURCE=${EMBER_LIBELF_SOURCE}\n",
        encoding="ascii",
    )
    argv = [str(make), "-r", "-m", str(mk), "-f", str(router_copy),
            "-f", str(query), "-n", "__libelf_route_query"]
    default = subprocess.run(argv, cwd=fixture_checkout, text=True,
                             capture_output=True)
    if default.returncode != 0 or str(default_source) not in default.stderr:
        raise RuntimeError(
            f"default source selection failed:\n{default.stdout}\n{default.stderr}"
        )

    absolute_source = temp / "prepared-source"
    write_fixture(absolute_source)
    absolute = subprocess.run(
        argv + [f"EMBER_LIBELF_SOURCE={absolute_source}"], cwd=fixture_checkout,
        text=True, capture_output=True,
    )
    if absolute.returncode != 0 or str(absolute_source) not in absolute.stderr:
        raise RuntimeError(
            f"absolute override failed:\n"
            f"{absolute.stdout}\n{absolute.stderr}"
        )

    spaced_source = temp / "prepared source with spaces"
    write_fixture(spaced_source)
    spaced = subprocess.run(
        argv + [f"EMBER_LIBELF_SOURCE={spaced_source}"], cwd=fixture_checkout,
        text=True, capture_output=True,
    )
    whitespace_error = "must not contain whitespace"
    if spaced.returncode == 0 or whitespace_error not in spaced.stderr:
        raise RuntimeError(
            f"whitespace override was not refused by the BSD .PATH guard:\n"
            f"{spaced.stdout}\n{spaced.stderr}"
        )

    relative = subprocess.run(
        argv + ["EMBER_LIBELF_SOURCE=relative/source"], cwd=fixture_checkout,
        text=True, capture_output=True,
    )
    relative_error = "must be an absolute prepared source path"
    if relative.returncode == 0 or relative_error not in relative.stderr:
        raise RuntimeError(
            f"relative override was not refused:\n{relative.stdout}\n{relative.stderr}"
        )

    missing = temp / "missing-source"
    missing.mkdir()
    invalid = subprocess.run(
        argv + [f"EMBER_LIBELF_SOURCE={missing}"], cwd=fixture_checkout,
        text=True, capture_output=True,
    )
    missing_error = "Missing prepared libelf input"
    if invalid.returncode == 0 or missing_error not in invalid.stderr:
        raise RuntimeError(
            f"missing prepared input was not refused:\n{invalid.stdout}\n{invalid.stderr}"
        )


def check_consumers(make, mk, query, source_root):
    for relative, group, expected in CONSUMERS:
        makefile = ROOT / relative
        cwd = ROOT / "tools/elftoolchain" if group == "ctftools" else makefile.parent
        values, stderr = query_make(make, mk, query, makefile, cwd, expected)
        for key, part in expected.items():
            value = values[key]
            if key == "EMBER_LIBELF_SOURCE":
                require_equal(relative, key, value, str(source_root))
                continue
            if key == "CPPFLAGS":
                common_include = f"-I{source_root}/common"
                libelf_include = f"-I{source_root}/libelf"
                if common_include not in value or libelf_include not in value:
                    raise RuntimeError(
                        f"{relative}: CPPFLAGS omit selected common/libelf roots: {value!r}"
                    )
                continue
            if key in ("SHLIB_MAJOR", "SHLIB_MINOR"):
                require_value(relative, key, value, part)
                continue
            if part == "dist/libdwarf":
                expected_path = ROOT / "external/bsd/elftoolchain/dist/libdwarf"
            else:
                expected_path = source_root / part
            require_equal(relative, key, value, str(expected_path))
        if group in ("external", "host", "ctftools") and "dist/libelf" in stderr:
            raise RuntimeError(f"{relative}: query referenced stale dist/libelf")
        print(f"PASS {relative}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", required=True,
                        help="explicit path to a BSD make implementation")
    args = parser.parse_args()
    make = Path(args.make).expanduser().resolve()
    if not make.is_file() or not os.access(make, os.X_OK):
        parser.error(f"--make is not an executable file: {make}")
    if not ROUTER.is_file():
        parser.error(f"source router is missing: {ROUTER}")

    with tempfile.TemporaryDirectory(prefix="emberbsd-libelf-routing-") as name:
        temp = Path(name)
        mk, query = write_make_support(temp)
        check_router_modes(make, temp, mk, query)
        prepared_source = ROOT / "external/bsd/elftoolchain/libelf-source"
        if not prepared_source.is_dir():
            parser.error(f"prepared default source is missing: {prepared_source}")
        check_consumers(make, mk, query, prepared_source)
    print("PASS libelf source routing: default, absolute, refusal, and 10 consumers")


if __name__ == "__main__":
    main()
