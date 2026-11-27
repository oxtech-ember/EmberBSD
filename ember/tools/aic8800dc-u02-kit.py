#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Identify a complete Luckfox Lyra Zero W normal-mode DC U02 factory kit."""
import argparse
import hashlib
from pathlib import Path
import struct

from aic8800dc_u02_files import read_regular

TABLES = "aic8800dc_u02_tables.bin"
FILES = (
    TABLES,
    "aic_powerlimit_8800dc.txt",
    "aic_userconfig_8800dc.txt",
    "fmacfw_calib_8800dc_u02.bin",
    "fmacfw_patch_8800dc_u02.bin",
    "fmacfw_patch_tbl_8800dc_u02.bin",
)
# Key each package by the original Linux driver and its complete asset set.
PROFILES = {
    "df86e12cd99327d776988ee792378f7fd8d82097c5fe3b57dc82bca785ae0693": (
        "g0a86431", "d427276066df05c67d514e80ee46fe8cf03a28ba3703ef596a2ccc2c80e9a05f"),
    "67c7324a1bb37a0c440ae4d4ad73f8451e39ffe6005a80bc892c6a7dd7159e73": (
        "261020 / g09cea8d", "682c79c1205796837970c190eb608fa40f12981c86a9a1bd6558826099daf780"),
}


def measure(path, limit):
    data = read_regular(path, limit)
    return len(data), hashlib.sha256(data).digest()


def identify(module_digest, assets):
    profile = PROFILES.get(module_digest)
    if profile is None:
        raise ValueError("unrecognized factory WLAN driver")
    if set(assets) != set(FILES):
        raise ValueError("incomplete factory asset set")
    # Fixed names, a terminator, a big-endian size and a binary SHA-256.
    result = hashlib.sha256(b"AIC8800DC-U02-STATION-KIT\x00\x01")
    for name in FILES:
        size, digest = assets[name]
        result.update(name.encode("ascii") + b"\0" + struct.pack(">Q", size) + digest)
    kit = result.hexdigest()
    if kit != profile[1]:
        raise ValueError("factory assets do not match the original WLAN driver")
    return profile[0], kit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True, type=Path, help="original aic8800_fdrv.ko")
    parser.add_argument("--firmware-dir", required=True, type=Path, help="original WLAN firmware directory")
    parser.add_argument("--tables", required=True, type=Path, help="output of aic8800dc-u02-tables.py")
    args = parser.parse_args()
    try:
        _, module = measure(args.driver, 323148)
        if module.hex() not in PROFILES:
            raise ValueError("unrecognized factory WLAN driver")
        assets = {name: measure(args.tables if name == TABLES else args.firmware_dir / name, 40284)
                  for name in FILES}
        release, digest = identify(module.hex(), assets)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Luckfox Lyra Zero W AIC8800DC U02 factory files match {release}")
    print(f"WLAN driver SHA-256 {module.hex()}")
    print(f"Station kit SHA-256 {digest}")


if __name__ == "__main__":
    main()
