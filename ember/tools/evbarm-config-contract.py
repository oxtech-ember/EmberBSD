#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Check actual evbarm config selection.
"""Check shared device selection with the native config tool."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def configure(config, source, tool, work):
    build = work / (config.stem + ".build")
    build.mkdir()
    result = subprocess.run([str(tool), "-s", str(source / "sys"),
                             "-b", str(build), str(config)],
                            cwd=source, text=True, capture_output=True)
    require(result.returncode == 0, result.stdout + result.stderr)
    makefile = (build / "Makefile").read_text()
    selected = re.findall(r"^\t([^\s]+\.c)(?: \\)?$", makefile, re.M)
    return selected, (build / "ioconf.c").read_text(), makefile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--nbconfig", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True,
                        help="Use an existing parent for temporary outputs.")
    args = parser.parse_args()
    source = args.source.resolve()
    reports = []
    with tempfile.TemporaryDirectory(prefix="evbarm-config-", dir=args.work) as tmp:
        work = Path(tmp)
        for name, suffix, adapter in (
                ("GENERIC", "", "arch/arm/xilinx/zynq_cemac.c"),
                ("EMBER64", "cemac* at acpi?\n", "dev/acpi/cemac_acpi.c")):
            config = work / name
            config.write_text('include "arch/evbarm/conf/' + name + '"\n' + suffix)
            files, ioconf, unused = configure(config, source, args.nbconfig, work)
            require(files.count("dev/cadence/if_cemac.c") == 1,
                    name + ": select the Cadence core exactly once")
            require(adapter in files, name + ": missing Cadence adapter")
            require("cemac_ca" in ioconf if name == "GENERIC"
                    else "cemac_acpi_ca" in ioconf,
                    name + ": missing Cadence attachment")
            reports.append({"config": name, "cadence_core": "selected once",
                            "adapter": adapter})
    print(json.dumps(reports, indent=2))


if __name__ == "__main__":
    main()
