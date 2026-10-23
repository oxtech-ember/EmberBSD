#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Execute the actual HPD provider chain.
"""Run bounded host HPD tests; this does not access display hardware."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess


def function(path, name):
    text = path.read_text()
    match = re.search(r"(?m)^(?:static )?(?:int|enum drm_connector_status)\n" +
                      name + r"\([\s\S]+?^\}", text)
    if match is None:
        raise ValueError("Function boundary changed: " + name)
    return text[:text.index("#include")] + match.group(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc-16")
    parser.add_argument("--cflags", default="")
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="Output directory owned exclusively by this run")
    args = parser.parse_args()
    owner = Path(__file__).resolve().parent.parent
    source = (owner / "tests/hdmi-hpd.c").read_text()
    for name, filename in (("native_detect", "hdmi_fdt"),
                           ("sun60i_a733_output_detect", "output_provider"),
                           ("sun60i_a733_de352_detect", "de352_fdt"),
                           ("a733_detect", "drm")):
        source = source.replace("/* " + name + " */", function(
            owner / ("sun60i_a733_" + filename + ".c"), name))
    args.build_dir.mkdir(parents=True, exist_ok=True)
    generated = args.build_dir.resolve() / "hpd.c"
    generated.write_text(source)
    binary = args.build_dir.resolve() / "hpd"
    command = shlex.split(args.cc) + ["-std=c11", "-Wall", "-Wextra", "-Werror"]
    command += shlex.split(args.cflags) + [str(generated), "-o", str(binary)]
    (args.build_dir / "compile_commands.json").write_text(json.dumps([{
        "directory": str(args.build_dir.resolve()), "file": str(generated),
        "arguments": command}], indent=2) + "\n")
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
