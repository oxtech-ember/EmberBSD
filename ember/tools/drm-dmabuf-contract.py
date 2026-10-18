#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Preserve the native map and lifetime controls.
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def extract(source, pattern):
    matches = re.findall(pattern, source, re.M | re.S)
    if len(matches) != 1:
        raise RuntimeError(f"Expected one actual source body: {pattern}")
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=("map", "lifetime", "all"),
                        nargs="?", default="all")
    args = parser.parse_args()
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("DRM_SOURCE_ROOT", tools.parent.parent))
    base = root / "sys/external/bsd/drm2"
    native = (base / "linux/linux_dma_buf.c").read_text()
    prime = (base / "dist/drm/drm_prime.c").read_text()
    gem = (base / "dist/drm/drm_gem.c").read_text()
    cases = ("map", "lifetime") if args.case == "all" else (args.case,)
    for case in cases:
        if case == "map":
            name = "drm-prime-map-contract.c"
            bodies = [extract(prime, r"^struct sg_table \*drm_gem_map_dma_buf\(.*?^}\n"),
                      extract(native, r"^static int\ndmabuf_fop_mmap\(.*?^}\n")]
        else:
            name = "drm-dmabuf-lifetime-contract.c"
            bodies = [extract(native, pattern) for pattern in (
                r"^struct dma_buf \*\ndma_buf_export\(.*?^}\n",
                r"^int\ndma_buf_fd\(.*?^}\n",
                r"^void\nget_dma_buf\(.*?^}\n",
                r"^void\ndma_buf_put\(.*?^}\n",
                r"^static int\ndmabuf_fop_close\(.*?^}\n")]
            bodies += [extract(gem, r"^static void drm_gem_object_exported_dma_buf_free\(.*?^}\n"),
                       extract(prime, r"^struct dma_buf \*drm_gem_dmabuf_export\(.*?^}\n"),
                       extract(prime, r"^void drm_gem_dmabuf_release\(.*?^}\n")]
        with tempfile.TemporaryDirectory(prefix="drm-dmabuf-") as directory:
            work = Path(directory)
            (work / "bodies.h").write_text("\n".join(bodies))
            # Preserve the existing doubles' unused callback parameters and
            # the map fixture's Linux/native signedness control.
            flags = ["-std=c11", "-g", "-Wall", "-Wextra", "-Werror",
                     "-Wno-unused-parameter", "-fsanitize=address,undefined",
                     "-fno-sanitize-recover=all"]
            flags += ["-pthread"] if case == "lifetime" else ["-Wno-sign-compare"]
            command = (shlex.split(os.environ.get("CC", "cc")) + flags +
                       shlex.split(os.environ.get("DRM_CFLAGS", "")) +
                       [f"-I{work}", str(tools / name), "-o", str(work / "check")])
            subprocess.run(command, check=True)
            subprocess.run([str(work / "check")], check=True)


if __name__ == "__main__":
    main()
