#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the production host-MLME BTM Request admission rule."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="Owned output directory; do not share it with another run")
    parser.add_argument("--cflags", default=os.environ.get("CFLAGS", ""))
    args = parser.parse_args()
    owner = Path(__file__).resolve().parent.parent
    args.build_dir.mkdir(parents=True, exist_ok=False)
    output = args.build_dir / "sta-soft"
    command = shlex.split(args.cc) + ["-std=c99", "-Wall", "-Wextra", "-Werror"]
    command += shlex.split(args.cflags)
    command += ["-I", str(owner), str(owner / "tests/sta-soft.c"),
                "-o", str(output)]
    subprocess.run(command, check=True)
    subprocess.run([str(output.resolve())], check=True)


if __name__ == "__main__":
    main()
