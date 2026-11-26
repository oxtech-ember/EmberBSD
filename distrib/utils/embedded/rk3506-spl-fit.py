#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Build the Rockchip 2017 SPL companion for the pinned Lyra Zero W loader."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

LOADER_HASH = "7a53323b5f2948caa377ae28a3d7a2971e2c28d5d4bb4328a473f0a1ccaba815"
# The combined image starts at sector 64. Its mainline FIT starts at 0x4000.
FIT_OFFSET = (0x4000 - 64) * 512


def run(tool, *args):
    command = os.environ.get("TOOL_" + tool.upper(), tool)
    return subprocess.check_output([command, *args], text=True).strip()


def verify(header, data, tee_os, tee_type):
    def prop(path, name, fmt="s"):
        return run("fdtget", "-t", fmt, header, path, name)

    def require(ok, text):
        if not ok:
            raise ValueError(text)

    require(set(run("fdtget", "-l", header, "/").split()) ==
            {"images", "configurations"}, "unexpected FIT root nodes")
    config = "/configurations/config-1"
    require(prop("/configurations", "default") == "config-1" and
            prop(config, "firmware") == "op-tee" and
            prop(config, "loadables") == "u-boot" and
            prop(config, "fdt") == "fdt-1", "unexpected FIT configuration")
    require(run("fdtget", "-l", header, "/configurations") == "config-1" and
            not run("fdtget", "-l", header, config),
            "configuration requires a different packing or signing procedure")
    images = {
        "u-boot": ("standalone", "u-boot", 0x100000),
        "op-tee": (tee_type, tee_os, 0x1000),
        "fdt-1": ("flat_dt", None, None),
    }
    require(set(run("fdtget", "-l", header, "/images").split()) == images.keys(),
            "unexpected FIT images")
    for node, (kind, image_os, address) in images.items():
        path = "/images/" + node
        require(run("fdtget", "-l", header, path) == "hash",
                f"{node} requires a different packing or signing procedure")
        require(prop(path, "type") == kind and
                prop(path, "compression") == "none", f"invalid {node} type")
        if address is not None:
            require(prop(path, "os") == image_os and
                    int(prop(path, "load", "x"), 16) == address and
                    int(prop(path, "entry", "x"), 16) == address,
                    f"invalid {node} handoff")
        offset = int(prop(path, "data-offset", "x"), 16)
        size = int(prop(path, "data-size", "x"), 16)
        require(0 < size <= len(data) - offset, f"invalid {node} extent")
        digest = bytes(
            int(v, 16) for v in prop(path + "/hash", "value", "bx").split())
        require(prop(path + "/hash", "algo") == "sha256" and
                hashlib.sha256(data[offset:offset + size]).digest() == digest,
                f"invalid {node} SHA-256")


def build(source, output):
    loader = source.read_bytes()
    if hashlib.sha256(loader).hexdigest() != LOADER_HASH:
        raise ValueError("loader does not match the selected RK3506B boot profile")
    fit = loader[FIT_OFFSET:]
    magic, header_len = struct.unpack_from(">II", fit)
    if magic != 0xd00dfeed or not 40 <= header_len < len(fit) or header_len % 512:
        raise ValueError("invalid external-data FIT header")
    data = fit[header_len:]
    with tempfile.TemporaryDirectory(dir=output.parent) as temp:
        hdr = Path(temp) / "original.dtb"
        hdr.write_bytes(fit[:header_len])
        verify(hdr, data, "tee", "tee")
        # arch/arm/mach-rockchip/make_fit_optee.sh uses this firmware ABI.
        run("fdtput", "-t", "s", hdr, "/images/op-tee", "os", "op-tee")
        run("fdtput", "-t", "s", hdr, "/images/op-tee", "type", "firmware")
        new = Path(temp) / "rockchip.itb"
        run("dtc", "-q", "-I", "dtb", "-O", "dtb", "-S", str(header_len),
            "-o", new, hdr)
        packed = new.read_bytes()
        if (len(packed) != header_len or
                struct.unpack_from(">I", packed, 4)[0] != header_len):
            raise ValueError("packing changed the external-data base")
        verify(new, data, "op-tee", "firmware")
        with new.open("ab") as stream:
            stream.write(data)
        new.replace(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("loader", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        build(args.loader, args.output)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(f"{parser.prog}: {error}")
