#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Prepare the normal-mode PHY tables from the selected Luckfox AIC8800DC BSP."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from aic8800dc_u02_files import read_regular

# Luckfox Lyra 261020 SDK, rkwifibt revision 223f24790d1ba8f96d35d19367ab933f81f82aa7.
SOURCES = {
    "aicwf_compat_8800dc.c": (80074,
        "c6acb29ebb6c0be349fa9e69a72768a6b3717f0eecdf8a90f4674e0766dba8c2"),
    "rwnx_main.c": (230240,
        "e84a5b13f5dcb36a552ae9ffc6010a3c31529c825cd5cb54e80f431d1f923bc1"),
}
TABLES = (
    ("usbcfg_tbl", 16),
    ("syscfg_tbl_8800dc", 16),
    ("syscfg_tbl_masked_8800dc", 192),
    ("patch_tbl_wifisetting", 16),
    ("ldpc_cfg_ram", 1340),
    ("agc_cfg_ram", 2048),
    ("txgain_map", 384),
    ("wifi_txgain_table_24g_8800dcdw", 128),
    ("wifi_txgain_table_24g_1_8800dcdw", 128),
    ("wifi_rxgain_table_24g_20m_8800dcdw", 256),
    ("wifi_rxgain_table_24g_40m_8800dcdw", 256),
)


def pack_tables(work):
    packed = bytearray(struct.pack("<8sHH", b"AICDCU02", 1, len(TABLES)))
    for name, size in TABLES:
        data = read_regular(work / name, size, exact=True)
        packed += struct.pack("<I", size) + data
    return packed


def prepare(bsp, bindir, output):
    sources = []
    for name, (size, digest) in SOURCES.items():
        data = read_regular(bsp / name, size, exact=True)
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"{name}: source differs from the supported BSP")
        sources.append(data.decode("utf-8"))
    source = "\n".join(sources)
    declarations = ["typedef unsigned int u32, uint32_t;", "#define CONFIG_VRF_DCDC_MODE 1"]
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        work = Path(temp)
        extract = [bindir / "llvm-objcopy"]
        for idx, (name, _) in enumerate(TABLES):
            pattern = rf"^(?:u32|uint32_t)\s+{name}\s*\[[^=]*=\s*\{{.*?\}};"
            match = re.search(pattern, source, re.M | re.S)
            if not match:
                raise ValueError(f"{name}: missing C initializer")
            section = f".aic{idx}"
            declarations.append(f'__attribute__((section("{section}"))) ' + match[0])
            extract += ["--dump-section", f"{section}={work / name}"]
        code, obj = work / "tables.c", work / "tables.o"
        code.write_text("\n".join(declarations) + "\n", encoding="utf-8")
        subprocess.run([bindir / "clang", "--target=armv7--netbsdelf-eabihf",
                        "-ffreestanding", "-Werror", "-c", code, "-o", obj], check=True)
        subprocess.run([*extract, obj, work / "extracted.o"], check=True)
        packed = pack_tables(work)
        if hashlib.sha256(packed).hexdigest() != (
                "7e934020dd15a26c162628132e59db8746ec9e23ef2086027071303a16b97025"):
            raise ValueError("generated tables differ from the supported factory files")
        pending = work / output.name
        pending.write_bytes(packed)
        os.replace(pending, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bsp", type=Path, required=True, help="Luckfox aic8800_fdrv source directory")
    parser.add_argument("--sdk-bin", type=Path, required=True, help="selected SDK tool directory")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        prepare(args.bsp, args.sdk_bin.resolve(), args.output)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
