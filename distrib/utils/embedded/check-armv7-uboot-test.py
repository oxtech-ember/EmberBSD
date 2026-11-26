#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Test the ARMv7 header checker against the selected host mkubootimage."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def main(mkubootimage):
    checker = Path(__file__).with_name("check-armv7-uboot.py")
    with tempfile.TemporaryDirectory() as directory:
        raw = Path(directory) / "netbsd.img"
        image = Path(directory) / "netbsd.ub"
        raw.write_bytes(b"ARM kernel payload fixture")
        subprocess.run([mkubootimage, "-A", "arm", "-O", "linux", "-C", "none",
                        "-T", "kernel_noload", "-a", "0", "-e", "0", "-t", "1",
                        "-n", "fixture", raw, image], check=True)
        check = [sys.executable, "-B", checker, image]
        subprocess.run(check, check=True)
        good = image.read_bytes()
        wrong_entry = bytearray(good)
        struct.pack_into(">I", wrong_entry, 20, 4)
        wrong_entry[4:8] = bytes(4)
        struct.pack_into(">I", wrong_entry, 4, zlib.crc32(wrong_entry[:64]))
        for name, data in (
            ("short header", good[:32]),
            ("header checksum", good[:4] + bytes(4) + good[8:]),
            ("entry", wrong_entry),
            ("truncated data", good[:-1]),
            ("trailing data", good + bytes(1)),
            ("data checksum", good[:-1] + bytes([good[-1] ^ 1])),
        ):
            image.write_bytes(data)
            if subprocess.run(check, capture_output=True).returncode == 0:
                raise SystemExit(f"checker accepted {name}")
    print("PASS: ARMv7 U-Boot header, entry, size and data checksums")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: check-armv7-uboot-test.py /path/to/nbmkubootimage")
    main(sys.argv[1])
