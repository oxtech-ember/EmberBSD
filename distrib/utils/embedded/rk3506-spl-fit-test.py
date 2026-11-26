#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check that only the two OP-TEE declarations differ between SPL images."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def check(loader):
    producer = Path(__file__).with_name("rk3506-spl-fit.py")
    get = os.environ.get("TOOL_FDTGET", "fdtget")
    put = os.environ.get("TOOL_FDTPUT", "fdtput")
    dtc = os.environ.get("TOOL_DTC", "dtc")
    # The original starts at card sector 64. The mainline SPL reads 0x4000.
    original = loader.read_bytes()[(0x4000 - 64) * 512:]
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        image = work / "rockchip.itb"
        subprocess.run([sys.executable, producer, loader, image], check=True)
        result = image.read_bytes()
        size = struct.unpack_from(">I", original, 4)[0]
        if struct.unpack_from(">I", result, 4)[0] != size or original[size:] != result[size:]:
            raise ValueError("the external data or its base changed")

        mainline = work / "mainline.dtb"
        rockchip = work / "rockchip.dtb"
        mainline.write_bytes(original[:size])
        rockchip.write_bytes(result[:size])
        for prop, expected in (("os", "op-tee"), ("type", "firmware")):
            args = [rockchip, "/images/op-tee", prop]
            actual = subprocess.check_output([get, "-t", "s", *args], text=True).strip()
            if actual != expected:
                raise ValueError(f"unexpected OP-TEE {prop}: {actual}")
            subprocess.run([put, "-t", "s", *args, "tee"], check=True)

        normalize = [dtc, "-q", "-s", "-I", "dtb", "-O", "dts"]
        if (subprocess.check_output([*normalize, rockchip]) !=
                subprocess.check_output([*normalize, mainline])):
            raise ValueError("additional FIT declarations changed")
    print("PASS: both SPL declarations, full payload area and remaining FIT tree")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("loader", type=Path)
    args = parser.parse_args()
    try:
        check(args.loader)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f"{parser.prog}: {error}")
