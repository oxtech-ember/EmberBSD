#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the production HCI boundary using the existing native core fixture."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess


def main(variant="hci"):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mode", choices=(("normal", "asan", "tsan", "sa", "cppcheck")
                                 if variant == "async" else
                                 ("normal", "asan", "sa", "cppcheck")),
                        default="normal")
    args = parser.parse_args()
    root, out = args.source.resolve(), args.output.resolve()
    if not (root / "sys/dev/ic" / f"rtw88_{variant}.h").is_file():
        symbol = "rtw88_async_reserve" if variant == "async" else f"rtw88_{variant}_ops"
        raise SystemExit(f"Missing production API: {symbol}")
    cc = shlex.split(os.environ.get("CC", ""))
    if not cc:
        raise SystemExit("Select the SDK compiler through CC")
    out.mkdir(parents=True, exist_ok=False)
    tools = root / "ember/tools"
    if variant != "async":
        subprocess.run([os.sys.executable, str(tools / "rtw88-state-contract.py"),
                        str(root), str(out)], check=True)
    include = out / "include"
    (include / "sys").mkdir(parents=True)
    (include / "dev").mkdir()
    (include / "dev/ic").symlink_to(root / "sys/dev/ic", target_is_directory=True)
    for header in ("param.h", "systm.h"):
        (include / "sys" / header).write_text(
            '#include <errno.h>\n#include "rtw88-test-kernel.h"\n', encoding="utf-8")
    if variant == "async":
        units = [tools / "rtw88-async-contract.c", root / "sys/dev/ic/rtw88_async.c"]
    else:
        units = [tools / f"rtw88-{variant}-contract.c", root / "sys/dev/ic/rtw88.c",
                 root / "sys/dev/pci/rtw88_pci_hci.c",
                 root / "sys/dev/pci/rtw88_pci_packet.c"]
        units += [root / "sys/dev/ic/rtw88_chip.c",
                  root / "sys/dev/ic/rtw8822c_runtime.c"]
        units += [root / "sys/dev/ic" / (name + ".c") for name in (
            "rtw88_net_policy", "rtw88_hw", "rtw8822c_hw", "rtw88_fw_init", "rtw8822c",
            "rtw8822c_boot", "rtw8822c_power", "rtw8822c_mac", "rtw8822c_phy",
            "rtw8822c_dack", "rtw8822c_table", "rtw8822c_coex", "rtw8822c_txpwr")]
    flags = ["-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
             "-DRTW88_START_CORE_ONLY", "-I" + str(out), "-I" + str(include),
             "-I" + str(tools), "-I" + str(root / "sys/dev/ic")]
    if variant == "async":
        flags += ["-pthread"]
    if args.mode == "tsan":
        flags += ["-fsanitize=thread", "-fno-sanitize-recover=all"]
    if args.mode == "asan":
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    commands = []

    def run(command, name, stdin=None):
        commands.append(command)
        with (out / (name + ".log")).open("w") as log:
            result = subprocess.run(command, input=stdin, text=True,
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SystemExit(f"FAIL: {name} returned {result.returncode}; {out / (name + '.log')}")

    try:
        if args.mode == "sa":
            for index, unit in enumerate(units):
                run(cc + flags + ["--analyze", "-Xanalyzer", "-analyzer-output=text",
                                  "-Xanalyzer", "-analyzer-werror", str(unit),
                                  "-o", str(out / f"analysis-{index}.plist")],
                    f"analysis-{index}")
        elif args.mode == "cppcheck":
            cppcheck = os.environ.get("CPPCHECK")
            if not cppcheck:
                raise SystemExit("Select the SDK cppcheck role through CPPCHECK")
            defines = subprocess.run(cc + ["-dM", "-E", "-"], input="", text=True,
                                     check=True, stdout=subprocess.PIPE).stdout
            (out / "predefines.h").write_text(defines, encoding="utf-8")
            run([cppcheck, "--enable=warning,performance,portability", "--check-level=exhaustive",
                 "--error-exitcode=1", "--include=" + str(out / "predefines.h"),
                 "-DRTW88_START_CORE_ONLY", "-I" + str(out), "-I" + str(include),
                 "-I" + str(tools), "-I" + str(root / "sys/dev/ic")] +
                [str(unit) for unit in units], "cppcheck")
        else:
            binary = out / f"{variant}-contract"
            run(cc + flags + [str(unit) for unit in units] + ["-o", str(binary)], "compile")
            run([str(binary)], "runtime")
            if variant == "chip":
                # Link the complete common units without PCI or RTL8822C.
                common = [tools / "rtw88-core-link-contract.c"] + [
                    root / "sys/dev/ic" / (name + ".c") for name in (
                        "rtw88", "rtw88_chip", "rtw88_fw_init", "rtw88_hw",
                        "rtw88_net_policy")]
                profiles = ([], ["-DRTW88_VHT_EXPERIMENT", "-DRTW88_DFS64_EXPERIMENT"],
                            ["-DRTW88_RFK_EXPERIMENT"])
                for index, defines in enumerate(profiles):
                    target = out / f"common-{index}"
                    run(cc + flags + defines + [str(unit) for unit in common] +
                        ["-o", str(target)], f"common-link-{index}")
                    run([str(target)], f"common-runtime-{index}")
    finally:
        (out / "commands.json").write_text(json.dumps(commands, indent=2) + "\n",
                                           encoding="utf-8")
    print(f"PASS: rtw88 {variant} {args.mode}; evidence {out}")

