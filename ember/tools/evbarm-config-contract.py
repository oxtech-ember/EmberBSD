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
    configured = {}
    with tempfile.TemporaryDirectory(prefix="evbarm-config-", dir=args.work) as tmp:
        work = Path(tmp)
        for name, suffix, adapter in (
                ("GENERIC", "", "arch/arm/xilinx/zynq_cemac.c"),
                ("EMBER64", "cemac* at acpi?\n", "dev/acpi/cemac_acpi.c")):
            config = work / name
            config.write_text('include "arch/evbarm/conf/' + name + '"\n' + suffix)
            files, ioconf, makefile = configure(config, source, args.nbconfig, work)
            configured[name] = (files, ioconf, makefile)
            require(files.count("dev/cadence/if_cemac.c") == 1,
                    name + ": select the Cadence core exactly once")
            require(adapter in files, name + ": missing Cadence adapter")
            require("cemac_ca" in ioconf if name == "GENERIC"
                    else "cemac_acpi_ca" in ioconf,
                    name + ": missing Cadence attachment")
            reports.append({"config": name, "cadence_core": "selected once",
                            "adapter": adapter})
        require("arch/arm/sunxi/sun60i_a733_hdmi_fdt.c" not in configured["GENERIC"][0],
                "GENERIC: common dwhdmi selected the A733 source without its providers")
        require("sun60i_a733_hdmi_ca" not in configured["GENERIC"][1],
                "GENERIC: common dwhdmi selected the A733 attachment")
        for name, remove in (("A733_ONLY", "dwhdmi"),
                             ("LEGACY_ONLY", "sun60ihdmi")):
            config = work / name
            config.write_text('include "arch/evbarm/conf/EMBER64"\nno ' +
                              remove + '\n')
            configured[name] = configure(config, source, args.nbconfig, work)
        native = "arch/arm/sunxi/sun60i_a733_hdmi_fdt.c"
        providers = ["sun60i_a733_ccu.c", "sun60i_a733_pck600.c",
                     "sun60i_a733_output.c", "sun60i_a733_output_provider.c",
                     "sun60i_a733_hdmi_phy.c", "sun60i_a733_tv0.c"]
        legacy = {"imx6_dwhdmi": "arch/arm/nxp/imx6_dwhdmi.c",
                  "rk_dwhdmi": "arch/arm/rockchip/rk_dwhdmi.c",
                  "sunxi_dwhdmi": "arch/arm/sunxi/sunxi_dwhdmi.c"}
        core = ["dev/ic/dw_hdmi.c", "dev/ic/dw_hdmi_phy.c",
                "dev/videomode/edid.c", "dev/videomode/videomode.c",
                "dev/i2c/ddc.c"]
        for name, (files, ioconf, makefile) in configured.items():
            enabled = name in ("EMBER64", "A733_ONLY")
            require((native in files) == enabled,
                    name + ": incorrect A733 HDMI source selection")
            require(("sun60i_a733_hdmi_ca" in ioconf) == enabled,
                    name + ": incorrect A733 HDMI attachment selection")
            for path in core:
                require(files.count(path) == 1, name + ": missing/duplicate " + path)
            for attach, path in legacy.items():
                require((path in files) == (name != "A733_ONLY"),
                        name + ": incorrect legacy source " + path)
                require((attach + "_ca" in ioconf) == (name != "A733_ONLY"),
                        name + ": incorrect legacy attachment " + attach)
            if enabled:
                for path in providers:
                    require("arch/arm/sunxi/" + path in files,
                            name + ": missing A733 provider " + path)
            if name == "GENERIC":
                require("arch/arm/sunxi/sun60i_a733_ccu.c" not in files,
                        "GENERIC: do not enable A733 CCU implicitly")
            require("OPT.dw_hdmi.c+= dwhdmi_core" in makefile and
                    "CPPFLAGS.dwhdmi_core+=${CPPFLAGS.drmkms}" in makefile,
                    name + ": missing common DRM build flags")
            reports.append({"config": name, "a733_hdmi": enabled,
                            "legacy_hdmi": name != "A733_ONLY",
                            "common_core": "selected once"})
    print(json.dumps(reports, indent=2))


if __name__ == "__main__":
    main()
