#!/usr/bin/env python3
"""Check compiled A733 DTB modes and the actual boot script control flow.

The shell fixture models U-Boot commands. It does not qualify a U-Boot parser,
a persistent loader entry, board detection, or physical recovery.
"""
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
import argparse
import os
from pathlib import Path
import struct
import subprocess


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def properties(path):
    """Read the property map from a trusted compiler-produced FDT."""
    data = path.read_bytes()
    magic, size, offset, strings, _, _, _, _, stringsize, blocksize = struct.unpack_from(
        ">10I", data
    )
    require(magic == 0xD00DFEED and size <= len(data), f"invalid FDT: {path}")
    require(strings + stringsize <= size and offset + blocksize <= size, "FDT bounds")
    end = offset + blocksize
    stack = []
    result = {}
    while offset < end:
        token, = struct.unpack_from(">I", data, offset)
        offset += 4
        if token == 1:
            nameend = data.index(b"\0", offset, end)
            stack.append(data[offset:nameend].decode("ascii"))
            offset = (nameend + 4) & ~3
        elif token == 2:
            stack.pop()
        elif token == 3:
            length, nameoff = struct.unpack_from(">II", data, offset)
            offset += 8
            require(nameoff < stringsize and offset + length <= end, "property bounds")
            nameend = data.index(b"\0", strings + nameoff, strings + stringsize)
            name = data[strings + nameoff:nameend].decode("ascii")
            key = ("/".join(stack) or "/", name)
            require(key not in result, f"duplicate property: {key}")
            result[key] = data[offset:offset + length]
            offset = (offset + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            require(not stack, "unclosed node")
            return result
        else:
            raise RuntimeError(f"invalid FDT token {token}")
    raise RuntimeError("missing FDT end")


SHELL = r'''
setenv() {
    case "$1" in ember_boot_mode|fdt_addr_r|bootargs|fdtname) ;;
    *) exit 90;; esac
    eval "$1=\$2"
}
mw_l() { echo "MW $*"; }
sleep() { :; }
setexpr_l() {
    if test "$1" = pins_up; then
        pins_up=$TEST_PINS_UP
    elif test "$1" = pins_down; then
        pins_down=$TEST_PINS_DOWN
    else
        exit 91
    fi
}
load() {
    echo "LOAD $*"
    case "$4" in *.dtb) test "$TEST_FAIL" != dtb;;
    */netbsd.img|netbsd.img) test "$TEST_FAIL" != kernel;;
    *) exit 92;; esac
}
fdt() { echo "FDT $*"; test "$TEST_FAIL" != "$1"; }
booti() { echo "BOOTI $*"; }
'''


def check_boot(script):
    # Only command spelling differs. Execute all production control flow.
    code = SHELL + script.replace("mw.l ", "mw_l ").replace("setexpr.l ", "setexpr_l ")
    cases = 0
    for prefix in ("", "/", "/boot/"):
        for board, up, down in (("zero3w", "7fff", "0"), ("zero4", "2", "1")):
            for mode in ("", "normal", "diagnostic", "invalid"):
                allowed = mode != "invalid" and not (mode == "diagnostic" and board == "zero4")
                for failure in (("", "dtb", "addr", "resize", "kernel") if allowed else ("",)):
                    env = dict(os.environ, ember_boot_mode=mode, prefix=prefix,
                               devtype="mmc", devnum="1", distro_bootpart="2",
                               TEST_PINS_UP=up, TEST_PINS_DOWN=down, TEST_FAIL=failure)
                    run = subprocess.run(["sh", "-c", code], env=env, text=True,
                                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
                    label = f"{prefix!r}/{board}/{mode!r}/{failure!r}"
                    lines = run.stdout.splitlines()
                    booted = [line for line in lines if line.startswith("BOOTI ")]
                    require(bool(booted) == (allowed and not failure), f"kernel entry: {label}")
                    require((run.returncode == 0) == bool(booted), f"exit status: {label}")
                    loads = [line for line in lines if line.startswith("LOAD ")]
                    if not allowed:
                        require(not loads, f"invalid mode loaded data: {label}")
                        if mode == "invalid":
                            require(not any(line.startswith("MW ") for line in lines), "invalid mode touched GPIO")
                    else:
                        suffix = "-diagnostic" if mode == "diagnostic" else ""
                        dtb = f"sun60i-a733-orangepi-{board}{suffix}.dtb"
                        require(loads[0] == f"LOAD mmc 1:2 0x4fa00000 {prefix}dtb/allwinner/{dtb}",
                                f"DTB path: {label}")
                        if failure in ("dtb", "addr", "resize"):
                            require(len(loads) == 1, f"continued after FDT failure: {label}")
                        else:
                            require(loads[1:] == [f"LOAD mmc 1:2 0x44000000 {prefix}netbsd.img"],
                                    f"kernel path: {label}")
                        if booted:
                            require(booted == ["BOOTI 0x44000000 - 0x4fa00000"], "boot addresses")
                    cases += 1
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb_directory", type=Path)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    trees = {name: properties(args.dtb_directory / f"sun60i-a733-orangepi-{name}.dtb")
             for name in ("zero3w", "zero3w-diagnostic", "zero4")}
    normal, diagnostic, zero4 = (trees[name] for name in ("zero3w", "zero3w-diagnostic", "zero4"))
    gpu = "/soc/gpu@1800000"
    observe = "netbsd,observe-only"
    for node in (gpu, "/display"):
        require((node, observe) not in normal, f"normal {node} must start")
        require(normal[node, "status"] == b"okay\0", f"normal {node} disabled")
        require(diagnostic[node, observe] == b"", f"diagnostic {node} must observe")
    expected = dict(normal)
    expected[gpu, observe] = b""
    expected["/display", observe] = b""
    require(diagnostic == expected, "diagnostic must change exactly the two mode properties")
    require(zero4[gpu, observe] == b"", "Zero 4 GPU mode changed")
    require(zero4["/display", "status"] == b"disabled\0", "Zero 4 display mode changed")
    require(normal["/", "compatible"] == b"xunlong,orangepi-zero3w\0allwinner,sun60i-a733\0",
            "Zero 3W identity changed")
    script = (args.source_root / "ember/boot/orangepi-zero4-boot.cmd").read_text()
    cases = check_boot(script)
    print(f"PASS: 3 compiled DTBs, exact 2-property diagnostic delta, {cases} boot mode/path/failure cases")


if __name__ == "__main__":
    main()
