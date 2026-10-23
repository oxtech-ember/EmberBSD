#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the production net80211 ioctl helpers with bounded host fixtures."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


def function(source, name):
    match = re.search(r"(?m)^(?:static )?int\n" + name + r"\([\s\S]+?^\}", source)
    if match is None:
        raise ValueError(f"Missing production function: {name}")
    return match.group(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc-16"))
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="Owned output directory; do not share it with another run")
    parser.add_argument("--cflags", default=os.environ.get("CFLAGS", ""))
    args = parser.parse_args()
    owner = Path(__file__).resolve().parent.parent
    abi = (owner / "ieee80211_ioctl.h").read_text()
    start = abi.index("#define IEEE80211_IOC_SAE")
    end = abi.index("\n};", abi.index("struct ieee80211req_sta_mgmt {", start)) + 3
    source = (owner / "ieee80211_ioctl.c").read_text()
    functions = "\n\n".join(function(source, name) for name in (
        "ieee80211_sae_request_valid", "ieee80211_sae_configure_check",
        "ieee80211_ioctl_sae",
        "ieee80211_sta_request_validate", "ieee80211_ioctl_sta_mgmt"))
    fixture = (owner / "tests/ioctl-contract.c").read_text()
    fixture = fixture.replace("/* PRODUCTION_ABI */", abi[start:end])
    fixture = source[:source.index("#include")] + "\n" + fixture
    fixture = fixture.replace("/* PRODUCTION_FUNCTIONS */", functions)
    driver = (owner.parent / "dev/ic/bwfm.c").read_text()
    fixture = driver[:driver.index("#include")] + "\n" + fixture
    fixture = fixture.replace("/* BWFM_FUNCTIONS */", "\n\n".join(
        function(driver, name) for name in ("bwfm_sae", "bwfm_sae_ioctl")))
    registers = (owner.parent / "dev/ic/bwfmreg.h").read_text()
    start = registers.index("struct bwfm_wsec_key {")
    end = registers.index("\n};", start) + 3
    fixture = fixture.replace("/* BWFM_KEY */", registers[start:end])
    args.build_dir.mkdir(parents=True, exist_ok=True)
    check = args.build_dir / "ioctl-contract.c"
    check.write_text(fixture)
    output = args.build_dir / "ioctl-contract"
    command = shlex.split(args.cc) + ["-std=c99", "-Wall", "-Wextra", "-Werror"]
    command += shlex.split(args.cflags) + [str(check), "-o", str(output)]
    (args.build_dir / "compile_commands.json").write_text(json.dumps([{
        "directory": str(args.build_dir.resolve()), "file": str(check.resolve()),
        "arguments": command}], indent=2) + "\n")
    subprocess.run(command, check=True)
    subprocess.run([str(output.resolve())], check=True)


if __name__ == "__main__":
    main()
