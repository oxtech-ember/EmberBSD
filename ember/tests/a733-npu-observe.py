#!/usr/bin/env python3
"""Run actual-source NPU attachment and smoke controls without hardware.

Origin: EmberBSD; AI-assisted. SPDX-License-Identifier: BSD-2-Clause
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile


def function(text, name, result):
    match = re.search(r"^" + re.escape(name) + r"\(.*?^}", text,
                      flags=re.MULTILINE | re.DOTALL)
    if match is None:
        raise RuntimeError("missing production function: " + name)
    return "static " + result + "\n" + match.group(0) + "\n"


def main():
    root = Path(__file__).resolve().parents[2]
    source = Path(os.environ.get("NPU_SOURCE_ROOT", root))
    driver = (source / "sys/arch/arm/sunxi/sun60i_a733_npu.c").read_text()
    fdt = (source / "sys/dev/fdt/fdtbus.c").read_text()
    dtsroot = source / "sys/external/gpl2/dts/dist/arch/arm64/boot/dts/allwinner"
    board = (dtsroot / "sun60i-a733-orangepi-zero3w.dts").read_text()
    soc = (dtsroot / "sun60i-a733.dtsi").read_text()
    override = re.search(r"&npu\s*\{([^}]+)};", board)
    if override is None or 'status = "okay";' not in override.group(1):
        raise RuntimeError("Zero 3W must enable its NPU node")
    for prop in ("netbsd,consumer-managed-power;", "netbsd,observe-only;"):
        if prop not in override.group(1):
            raise RuntimeError("Zero 3W must preserve " + prop)
    npu = re.search(r"npu: npu@3600000\s*\{([^}]+)};", soc).group(1)
    if 'status = "disabled";' not in npu or "pinctrl" in npu:
        raise RuntimeError("other A733 boards or NPU pinctrl changed")
    with tempfile.TemporaryDirectory(prefix="a733-npu-observe-") as temp:
        work = Path(temp)
        print("Temporary outputs: " + str(work), flush=True)
        (work / "npu-attach.h").write_text(function(driver, "sun60i_npu_attach", "void"))
        (work / "fdt-attach.h").write_text(function(fdt, "fdt_pre_attach", "int"))
        abi = []
        for name in ("envelope", "init", "database", "reg", "driver_status"):
            abi.append(re.search(r"^struct vip_drv_" + name + r" \{.*?^};",
                                 driver, re.MULTILINE | re.DOTALL).group(0))
        (work / "vip-abi.h").write_text("\n".join(abi) + "\n")
        (work / "npu-register-defs.h").write_text("\n".join(
            line for line in driver.splitlines()
            if re.match(r"#define (A733_NPU_|VIP_STATUS_)", line)) + "\n")
        (work / "npu-registers.h").write_text(
            function(driver, "sun60i_npu_read_ids", "int") +
            (function(driver, "sun60i_npu_mmio_ready", "int")
             if "sun60i_npu_mmio_ready(" in driver else "") +
            function(driver, "sun60i_npu_vip_read_reg", "int"))
        (work / "npu-power.h").write_text(
            function(driver, "sun60i_npu_power_node", "int") +
            function(driver, "sun60i_npu_read_ids", "int") +
            function(driver, "sun60i_npu_stage_power_id", "int"))
        (work / "npu-preflight.h").write_text(
            function(driver, "sun60i_npu_stage_domain", "int") +
            function(driver, "sun60i_npu_stage_clocks", "int"))
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter"]
        flags += shlex.split(os.environ.get("NPU_TEST_CFLAGS", ""))
        compiler = shlex.split(os.environ.get("CC", "cc"))
        for name in ("a733-npu-observe-contract", "a733-npu-vip-smoke-contract",
                     "a733-npu-register-contract", "a733-npu-power-contract",
                     "a733-npu-preflight-contract"):
            binary = work / name
            subprocess.run(compiler + flags + ["-I" + str(work),
                str(root / "ember/tools" / (name + ".c")), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            if result.returncode:
                print(result.stdout + result.stderr, end="")
                result.check_returncode()
            print(result.stdout.strip().splitlines()[-1])
    mmu_env = dict(os.environ, NPU_MMU_SOURCE_ROOT=str(source))
    mmu_env.setdefault("NPU_MMU_CFLAGS", os.environ.get("NPU_TEST_CFLAGS", ""))
    subprocess.run([sys.executable, str(root / "ember/tools/a733-npu-mmu-contract.py")],
                   env=mmu_env, check=True)
    print("PASS: Zero 3W NPU DT scope and source controls")


if __name__ == "__main__":
    main()
