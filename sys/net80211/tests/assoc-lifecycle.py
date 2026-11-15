#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the production association identity helpers with a host fixture."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess


def function(source, name):
    match = re.search(r"(?m)^(?:static )?(?:int|bool|void)\n" + name +
                      r"\([\s\S]+?^}", source)
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
    source = (owner / "ieee80211_node.c").read_text()
    helpers = "\n\n".join(function(source, name) for name in (
        "ieee80211_assoc_epoch_assign", "ieee80211_assoc_epoch_clear",
        "ieee80211_assoc_event_snapshot"))
    fixture = (owner / "tests/assoc-lifecycle-contract.c").read_text()
    netbsd = (owner / "ieee80211_netbsd.h").read_text()
    event_abi = re.search(r"(?m)^struct ieee80211_assoc_event \{[\s\S]+?^\};",
                          netbsd)
    if event_abi is None:
        raise ValueError("Missing production association event ABI")
    fixture = fixture.replace("/* ASSOCIATION_EVENT_ABI */",
        event_abi.group(0))
    fixture = fixture.replace("/* LIFECYCLE_HELPERS */", helpers)
    proto = (owner / "ieee80211_proto.c").read_text()
    retry_wiring = re.findall(
        r"ieee80211_sta_leave\(ic, ni\);[\s\S]{0,320}?"
        r"ieee80211_assoc_epoch_assign\(ic,\s*ni\)", proto)
    if len(retry_wiring) != 2:
        raise ValueError("RUN-to-AUTH/ASSOC retries must stamp after leave")
    ioctl = (owner / "ieee80211_ioctl.c").read_text()
    fixture = fixture.replace("/* IOCTL_RESULT */",
        function(ioctl, "ieee80211_ioctl_assoc_result"))
    fixture = fixture.replace("/* MLME_VALID */",
        function(ioctl, "ieee80211_mlme_request_valid"))
    args.build_dir.mkdir(parents=True, exist_ok=True)
    check = args.build_dir / "assoc-lifecycle-contract.c"
    check.write_text(fixture)
    output = args.build_dir / "assoc-lifecycle-contract"
    command = shlex.split(args.cc) + ["-std=c99", "-Wall", "-Wextra", "-Werror"]
    command += shlex.split(args.cflags) + [str(check), "-o", str(output)]
    subprocess.run(command, check=True)
    runner = shlex.split(os.environ.get("TEST_RUNNER", ""))
    subprocess.run(runner + [str(output.resolve())], check=True)

    ioctl_fixture = (owner / "tests/assoc-ioctl-contract.c").read_text()
    mlme_abi = re.search(r"(?m)^struct ieee80211req_mlme \{[\s\S]+?^\};",
                         (owner / "ieee80211_ioctl.h").read_text())
    if mlme_abi is None:
        raise ValueError("Missing production MLME request ABI")
    ioctl_fixture = ioctl_fixture.replace("/* MLME_ABI */", mlme_abi.group(0))
    ioctl_fixture = ioctl_fixture.replace("/* NODE_HELPERS */", "\n\n".join(
        function(source, name) for name in (
            "ieee80211_assoc_epoch_assign", "ieee80211_assoc_epoch_clear",
            "ieee80211_assoc_event_snapshot")))
    ioctl_fixture = ioctl_fixture.replace("/* IOCTL_HELPERS */", "\n\n".join(
        function(ioctl, name) for name in (
            "ieee80211_mlme_request_valid", "ieee80211_ioctl_assoc_result",
            "ieee80211_ioctl_setmlme")))
    ioctl_check = args.build_dir / "assoc-ioctl-contract.c"
    ioctl_check.write_text(ioctl_fixture)
    ioctl_output = args.build_dir / "assoc-ioctl-contract"
    ioctl_command = shlex.split(args.cc) + ["-std=c99", "-Wall", "-Wextra",
        "-Werror"] + shlex.split(args.cflags) + [str(ioctl_check), "-o",
        str(ioctl_output)]
    subprocess.run(ioctl_command, check=True)
    subprocess.run(runner + [str(ioctl_output.resolve())], check=True)


if __name__ == "__main__":
    main()
