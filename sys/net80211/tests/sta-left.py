#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the production STA_LEFT transition rule with the station scan guard."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess


def function(source, name):
    match = re.search(r"(?m)^(?:static )?(?:int|void)\n" + name +
                      r"\([\s\S]+?^\}", source)
    if match is None:
        raise ValueError(f"Missing production function: {name}")
    return match.group(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="Owned output directory; do not share it with another run")
    parser.add_argument("--cflags", default=os.environ.get("CFLAGS", ""))
    args = parser.parse_args()
    owner = Path(__file__).resolve().parent.parent
    var = (owner / "ieee80211_var.h").read_text()
    flag = re.search(r"(?m)^#define\s+IEEE80211_FEXT_STA_LEFT\s+\S+", var)
    if flag is None:
        raise ValueError("Missing IEEE80211_FEXT_STA_LEFT")
    proto = (owner / "ieee80211_proto.c").read_text()
    ioctl = (owner / "ieee80211_ioctl.c").read_text()
    code = "\n\n".join([flag.group(0),
        function(proto, "ieee80211_sta_left_transition"),
        function(ioctl, "ieee80211_setupscan"),
        function(ioctl, "ieee80211_startscan")])
    fixture = (owner / "tests/sta-left.c").read_text()
    fixture = fixture.replace("/* PRODUCTION */", code)
    args.build_dir.mkdir(parents=True, exist_ok=False)
    check = args.build_dir / "sta-left.c"
    check.write_text(fixture)
    output = args.build_dir / "sta-left"
    command = shlex.split(args.cc) + ["-std=c99", "-Wall", "-Wextra", "-Werror"]
    command += shlex.split(args.cflags) + [str(check), "-o", str(output)]
    subprocess.run(command, check=True)
    subprocess.run([str(output.resolve())], check=True)


if __name__ == "__main__":
    main()
