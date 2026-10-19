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


def prepare_sync(root, base, work):
    def function(source, name):
        return extract(source, rf"^(?:static )?(?:struct [\w]+ \*|[\w]+)\n{name}\(.*?^}}\n")

    header = (base / "include/linux/dma-resv.h").read_text()
    layout = "\n".join(extract(header, rf"^struct {name} \{{.*?^}};")
                       for name in ("dma_resv", "dma_resv_list"))
    header = (base / "include/linux/sync_file.h").read_text()
    layout += "\n" + extract(header, r"^struct sync_file \{.*?^};")
    (work / "sync-layout.h").write_text(layout)
    source = (base / "linux/linux_dma_resv.c").read_text()
    bodies = "struct dma_resv_write_ticket {};\n"
    for name in ("objlist_alloc", "objlist_free", "dma_resv_reserve_shared",
                 "dma_resv_write_begin", "dma_resv_write_commit",
                 "dma_resv_add_excl_fence", "dma_resv_add_shared_fence",
                 "dma_resv_replace_excl_fence", "dma_resv_get_excl",
                 "dma_resv_get_list"):
        bodies += function(source, name).replace("dma_resv_reserve_shared(",
                                                "production_reserve_shared(")
    (work / "resv-bodies.h").write_text(bodies)
    source = (base / "linux/linux_sync_file.c").read_text()
    bodies = "".join(function(source, name).replace("sync_file_create(",
                                                 "production_sync_file_create(")
                     for name in ("sync_file_create", "sync_file_close",
                                  "sync_file_get_fence"))
    (work / "sync-file-bodies.h").write_text(bodies)
    source = (base / "linux/linux_dma_buf.c").read_text()
    bodies = extract(source, r"^struct dmabuf_fence_vector \{.*?(?=^struct dma_buf \*\ndma_buf_export\()")
    (work / "ioctl-bodies.h").write_text(bodies)
    dispatch = extract(source, r"\.fo_ioctl = (\w+),")
    (work / "ioctl-dispatch.h").write_text(
        "static int (*actual_ioctl)(struct file *, u_long, void *) = " +
        dispatch + ";\n")
    (work / "dmabuf-abi.h").write_bytes((root / "sys/sys/dmabuf.h").read_bytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case", choices=("map", "lifetime", "array", "sync", "all"),
                        nargs="?", default="all")
    args = parser.parse_args()
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("DRM_SOURCE_ROOT", tools.parent.parent))
    base = root / "sys/external/bsd/drm2"
    native = (base / "linux/linux_dma_buf.c").read_text()
    prime = (base / "dist/drm/drm_prime.c").read_text()
    gem = (base / "dist/drm/drm_gem.c").read_text()
    cases = ("map", "lifetime", "array", "sync") if args.case == "all" else (args.case,)
    for case in cases:
        if case == "map":
            name = "drm-prime-map-contract.c"
            bodies = [extract(prime, r"^struct sg_table \*drm_gem_map_dma_buf\(.*?^}\n"),
                      extract(native, r"^static int\ndmabuf_fop_mmap\(.*?^}\n")]
        elif case == "lifetime":
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
        else:
            name = ("drm-fence-array-contract.c" if case == "array"
                    else "drm-dmabuf-sync-contract.c")
            source = (base / "linux/linux_dma_fence_array.c").read_text()
            bodies = [source[source.index("static const char *"):]]
        with tempfile.TemporaryDirectory(prefix="drm-dmabuf-") as directory:
            work = Path(directory)
            (work / "bodies.h").write_text("\n".join(bodies))
            if case in ("array", "sync"):
                header = (base / "include/linux/dma-fence-array.h").read_text()
                layout = "\n".join(extract(header, rf"^struct {name} \{{.*?^}};")
                                   for name in ("dma_fence_array_cb", "dma_fence_array"))
                (work / "layout.h").write_text(layout + "\n#define ARRAY_MODE_FIELD 1\n")
            if case == "sync":
                prepare_sync(root, base, work)
            # Preserve the existing doubles' unused callback parameters and
            # the map fixture's Linux/native signedness control.
            flags = ["-std=gnu11" if case == "sync" else "-std=c11", "-g", "-Wall", "-Wextra", "-Werror",
                     "-Wno-unused-parameter", "-fsanitize=address,undefined",
                     "-fno-sanitize-recover=all"]
            flags += ["-pthread"] if case in ("lifetime", "array", "sync") else ["-Wno-sign-compare"]
            command = (shlex.split(os.environ.get("CC", "cc")) + flags +
                       shlex.split(os.environ.get("DRM_CFLAGS", "")) +
                       [f"-I{work}", str(tools / name), "-o", str(work / "check")])
            subprocess.run(command, check=True)
            subprocess.run([str(work / "check")], check=True)


if __name__ == "__main__":
    main()
