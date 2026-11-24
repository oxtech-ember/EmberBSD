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
            for mode in ("", "normal", "diagnostic", "graphics", "npu-observe", "invalid"):
                allowed = (mode in ("", "normal")
                           or (mode == "diagnostic" and board == "zero3w")
                           or (mode in ("graphics", "npu-observe") and board == "zero4"))
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
                        suffix = f"-{mode}" if mode in ("diagnostic", "graphics", "npu-observe") else ""
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


def phandles(tree):
    nodes = [(value, node) for (node, name), value in tree.items() if name == "phandle"]
    handles = dict(nodes)
    require(len(handles) == len(nodes), "duplicate provider phandle")
    return handles


def resolve_references(tree, handles, data, argument_count):
    """Return provider paths and literal arguments for a known binding."""
    refs = []
    offset = 0
    while offset < len(data):
        handle = data[offset:offset + 4]
        require(handle in handles, "unknown provider phandle")
        provider = handles[handle]
        count = argument_count
        if isinstance(count, str):
            count, = struct.unpack(">I", tree[provider, count])
        end = offset + 4 + count * 4
        require(end <= len(data), "truncated provider arguments")
        refs.append((provider, data[offset + 4:end]))
        offset = end
    return refs


REFERENCE_ARGS = {
    "clocks": "#clock-cells", "iommus": "#iommu-cells",
    "nsi": 1, "phy-handle": 0, "pinctrl-0": 0, "syscon": 0,
    "resets": "#reset-cells", "mmc-pwrseq": 0, "phys": "#phy-cells",
    "gpio": "#gpio-cells", "reset-gpios": "#gpio-cells",
    "gpu-supply": 0, "vbus-supply": 0, "vqmmc-supply": 0,
}


def check_zero4_graphics(normal, graphics):
    sys_cfg = "/soc/syscon@3000000"
    hdmi = "/soc/hdmi@5520000"
    engine = "/soc/display-engine@5000000"
    display = "/display"
    gpu = "/soc/gpu@1800000"
    enabled = (sys_cfg, hdmi, engine, display)
    for node in enabled:
        require(normal[node, "status"] == b"disabled\0", f"Zero 4 normal enabled {node}")
        require(graphics[node, "status"] == b"okay\0", f"Zero 4 graphics disabled {node}")

    zero4_compatible = b"xunlong,orangepi-zero4\0allwinner,sun60i-a733\0"
    for tree in (normal, graphics):
        require(tree["/", "compatible"] == zero4_compatible, "Zero 4 identity changed")

    require(graphics[hdmi, "allwinner,external-resistor"] == b"", "HDMI resistor binding")
    require(graphics[hdmi, "syscon"] == graphics[sys_cfg, "phandle"], "HDMI SYS_CFG binding")
    require(graphics[engine, "output"] == graphics[hdmi, "phandle"], "DE native output binding")
    require(graphics[display, "display-engine"] == graphics[engine, "phandle"], "KMS DE binding")
    for consumer, supply, regulator in ((hdmi, "vdd08-supply", b"dcdc2\0"),
                                        (hdmi, "vcc18-supply", b"cldo2\0"),
                                        (gpu, "gpu-supply", b"dcdc4\0")):
        targets = [node for (node, name), value in graphics.items()
                   if name == "regulator-name" and value == regulator]
        require(len(targets) == 1, f"Zero 4 regulator binding: {regulator!r}")
        require(graphics[consumer, supply] == graphics[targets[0], "phandle"],
                f"{consumer} regulator binding: {supply}")

    # Compare all other properties byte for byte, except for phandle values
    # in known binding slots. Keep literal cells and provider arguments intact.
    added = {(hdmi, name) for name in
             ("vdd08-supply", "vcc18-supply", "allwinner,external-resistor")}
    added.add((engine, "output"))
    removed = {(gpu, "netbsd,observe-only")}
    normal_keys = {key for key in normal if key[1] != "phandle"}
    graphics_keys = {key for key in graphics if key[1] != "phandle"}
    require(graphics_keys == (normal_keys - removed) | added, "Zero 4 graphics property scope")

    normal_handles = phandles(normal)
    graphics_handles = phandles(graphics)
    for key in sorted(normal_keys - removed):
        node, name = key
        if node in enabled and name == "status":
            continue
        if name in REFERENCE_ARGS:
            count = REFERENCE_ARGS[name]
            require(resolve_references(normal, normal_handles, normal[key], count) ==
                    resolve_references(graphics, graphics_handles, graphics[key], count),
                    f"Zero 4 graphics changed providers or arguments for {node} {name}")
        else:
            require(normal[key] == graphics[key], f"Zero 4 graphics changed {node} {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dtb_directory", type=Path)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    trees = {name: properties(args.dtb_directory / f"sun60i-a733-orangepi-{name}.dtb")
             for name in ("zero3w", "zero3w-diagnostic", "zero4", "zero4-graphics", "zero4-npu-observe")}
    normal = trees["zero3w"]
    diagnostic = trees["zero3w-diagnostic"]
    zero4 = trees["zero4"]
    graphics = trees["zero4-graphics"]
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
    check_zero4_graphics(zero4, graphics)
    npu = "/soc/npu@3600000"
    require(zero4[npu, "status"] == b"disabled\0", "Zero 4 normal enabled NPU")
    require(zero4[npu, observe] == b"", "Zero 4 NPU mode changed")
    require(zero4[npu, "netbsd,consumer-managed-power"] == b"",
            "Zero 4 NPU power ownership changed")
    npu_observe = trees["zero4-npu-observe"]
    opp_table = "/opp-table-npu"
    opp = opp_table + "/opp-492000000"
    handles = phandles(npu_observe)
    dcdc2 = [node for (node, name), value in npu_observe.items()
             if name == "regulator-name" and value == b"dcdc2\0"]
    require(len(dcdc2) == 1, "Zero 4 NPU DCDC2 supply")
    added = {
        (npu, "npu-supply"): npu_observe[dcdc2[0], "phandle"],
        (npu, "operating-points-v2"): npu_observe[opp_table, "phandle"],
        (opp_table, "compatible"): b"operating-points-v2\0",
        (opp, "opp-hz"): struct.pack(">Q", 492000000),
        (opp, "opp-microvolt"): struct.pack(">I", 800000),
    }
    require({key for key in npu_observe if key[1] != "phandle"} ==
            {key for key in zero4 if key[1] != "phandle"} | added.keys(),
            "Zero 4 NPU observe property scope")
    for key, value in added.items():
        require(npu_observe[key] == value, f"Zero 4 NPU binding: {key}")
    require(npu_observe[npu, "status"] == b"okay\0", "Zero 4 NPU observe disabled")
    normal_handles = phandles(zero4)
    require(set(handles.values()) == set(normal_handles.values()) | {opp_table, dcdc2[0]},
            "Zero 4 NPU observe changed provider scope")
    for key, value in zero4.items():
        if key == (npu, "status") or key[1] == "phandle":
            continue
        if key[1] in REFERENCE_ARGS:
            count = REFERENCE_ARGS[key[1]]
            require(resolve_references(zero4, normal_handles, value, count) ==
                    resolve_references(npu_observe, handles, npu_observe[key], count),
                    f"Zero 4 NPU observe changed providers or arguments: {key}")
        else:
            require(npu_observe[key] == value, f"Zero 4 NPU observe changed {key}")
    for node in (gpu, "/display"):
        require((node, observe) not in graphics, f"Zero 4 graphics {node} must start")
    require(normal["/", "compatible"] == b"xunlong,orangepi-zero3w\0allwinner,sun60i-a733\0",
            "Zero 3W identity changed")
    script = (args.source_root / "ember/boot/orangepi-zero4-boot.cmd").read_text()
    cases = check_boot(script)
    print(f"PASS: 5 compiled DTBs, Zero 4 bindings, exact 2-property diagnostic delta, {cases} boot mode/path/failure cases")


if __name__ == "__main__":
    main()
