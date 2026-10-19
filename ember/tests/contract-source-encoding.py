#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Exercise the owning runners in an ASCII locale.
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
files = """ember/tools/bcm2712-v3d-prime-contract.rb
ember/tools/bcm2712-v3d-prime-contract.c
ember/tools/drm-dmabuf-contract.py
ember/tools/drm-prime-map-contract.c
ember/tools/drm-dmabuf-lifetime-contract.c
ember/tools/drm-fence-array-contract.c
ember/tools/drm-dmabuf-sync-contract.c
sys/external/bsd/drm2/bcmv3d/bcmv3d_drv.c
sys/external/bsd/drm2/linux/linux_dma_buf.c
sys/external/bsd/drm2/dist/drm/drm_prime.c
sys/external/bsd/drm2/dist/drm/drm_gem.c
sys/external/bsd/drm2/linux/linux_dma_fence_array.c
sys/external/bsd/drm2/include/linux/dma-fence-array.h
sys/external/bsd/drm2/include/linux/dma-resv.h
sys/external/bsd/drm2/include/linux/sync_file.h
sys/external/bsd/drm2/linux/linux_dma_resv.c
sys/external/bsd/drm2/linux/linux_sync_file.c
sys/sys/dmabuf.h""".splitlines()

with tempfile.TemporaryDirectory(prefix="ember-source-encoding-") as directory:
    root = Path(directory)
    for name in files:
        destination = root / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / name, destination)
    # Comments inside actual extracted bodies test both source decoding and
    # generated-header encoding, without replacing the behavioral contracts.
    for name, function in (
        ("bcmv3d/bcmv3d_drv.c", "bcmv3d_prime_mmap"),
        ("linux/linux_dma_buf.c", "dmabuf_fop_mmap"),
    ):
        path = root / "sys/external/bsd/drm2" / name
        source, count = re.subn(
            rf"(\n{function}\(.*?\n\{{\n)",
            r"\1\t/* UTF-8 source comment: café. */\n",
            path.read_text(encoding="utf-8"), count=1, flags=re.S,
        )
        if count != 1:
            raise RuntimeError(f"Missing actual source body: {function}")
        path.write_text(source, encoding="utf-8")
    env = dict(os.environ, LC_ALL="C", PYTHONCOERCECLOCALE="0", PYTHONUTF8="0",
               DRM_SOURCE_ROOT=str(root))
    tools = root / "ember/tools"
    subprocess.run(shlex.split(os.environ.get("RUBY", "ruby")) +
                   ["-E", "US-ASCII", str(tools / "bcm2712-v3d-prime-contract.rb")],
                   env=env, check=True)
    subprocess.run([sys.executable, str(tools / "drm-dmabuf-contract.py"), "all"],
                   env=env, check=True)
print("PASS: actual PRIME and DMA-BUF runners preserve UTF-8 under an ASCII locale")
