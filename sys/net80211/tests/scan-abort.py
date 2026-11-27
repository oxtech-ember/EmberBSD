#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the production scan abort rule and check its use at SCAN to INIT."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess


def function(source, name):
    match = re.search(r"(?m)^void\n" + name + r"\([\s\S]+?^\}", source)
    if match is None:
        raise ValueError(f"Missing production function: {name}")
    return "static " + match.group(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="Owned output directory; do not share it with another run")
    parser.add_argument("--cflags", default=os.environ.get("CFLAGS", ""))
    args = parser.parse_args()
    owner = Path(__file__).resolve().parent.parent
    node = (owner / "ieee80211_node.c").read_text()
    proto = (owner / "ieee80211_proto.c").read_text()
    # The INIT transition from SCAN must end the scan through the rule.
    arm = re.search(r"case IEEE80211_S_SCAN:\n(?:\t+/\*[^\n]*\*/\n)?"
                    r"\t+ieee80211_abort_scan\(ic\);\n\t+goto reset;", proto)
    if arm is None:
        raise SystemExit("SCAN to INIT does not call ieee80211_abort_scan()")
    code = "\n\n".join([function(node, "ieee80211_cancel_scan"),
                        function(node, "ieee80211_abort_scan")])
    fixture = (owner / "tests/scan-abort.c").read_text()
    fixture = fixture.replace("/* PRODUCTION */", code)
    args.build_dir.mkdir(parents=True, exist_ok=False)
    check = args.build_dir / "scan-abort.c"
    check.write_text(fixture)
    output = args.build_dir / "scan-abort"
    command = shlex.split(args.cc) + ["-std=c99", "-Wall", "-Wextra", "-Werror"]
    command += shlex.split(args.cflags) + [str(check), "-o", str(output)]
    subprocess.run(command, check=True)
    subprocess.run([str(output.resolve())], check=True)


if __name__ == "__main__":
    main()
