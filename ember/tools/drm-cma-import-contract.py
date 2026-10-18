#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted; execute the actual native import bodies.
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(source, name):
    pattern = (r"^(?:static (?:inline )?)?(?:struct [a-z_]+ \*|int|void)\n" +
               re.escape(name) + r"\(.*?^}\n")
    matches = re.findall(pattern, source, re.M | re.S)
    if len(matches) != 1:
        raise RuntimeError(f"Expected exactly one function: {name}")
    return matches[0]


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("CMA_SOURCE_ROOT", tools.parent.parent))
    base = root / "sys/external/bsd/drm2"
    header = (base / "include/drm/bus_dma_hacks.h").read_text()
    cma = (base / "drm/drm_gem_cma_helper.c").read_text()
    bodies = function(header, "bus_dmamem_import_pages")
    for name in ("drm_gem_cma_create_internal", "drm_gem_cma_create",
                 "drm_gem_cma_obj_free", "drm_gem_cma_free_object"):
        bodies += function(cma, name)
    with tempfile.TemporaryDirectory(prefix="drm-cma-import-") as directory:
        work = Path(directory)
        (work / "bodies.h").write_text(bodies)
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("CMA_CFLAGS", "")) +
                   [f"-I{work}", str(tools / "drm-cma-import-contract.c"),
                    "-o", str(work / "test")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")] +
                       shlex.split(os.environ.get("CMA_CASE", "")), check=True)


if __name__ == "__main__":
    main()
