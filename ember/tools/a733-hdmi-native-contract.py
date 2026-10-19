#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted.
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def body(path):
    return "".join(line for line in path.read_text().splitlines(keepends=True)
                   if not line.startswith("#include ")
                   and not line.startswith("__KERNEL_RCSID"))


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("A733_HDMI_NATIVE_SOURCE_ROOT", tools.parent.parent))
    source = root / "sys/arch/arm/sunxi"
    with tempfile.TemporaryDirectory(prefix="a733-hdmi-native-") as directory:
        work = Path(directory)
        headers = work / "arm/sunxi"
        headers.mkdir(parents=True)
        for name in ("sun60i_a733_output.h", "sun60i_a733_hdmi_phy.h",
                     "sun60i_a733_tv0.h", "sun60i_a733_ccu.h", "sunxi_rtcvar.h"):
            (headers / name).write_bytes((source / name).read_bytes())
        for name in ("videomode.h", "edidvar.h"):
            (work / name).write_text(body(root / "sys/dev/videomode" / name))
        for name in ("sun60i_a733_output.c", "sun60i_a733_hdmi_fdt.c"):
            (work / name).write_text(body(source / name))
        # Keep the actual fixed table and complete plan function unchanged.
        # Hardware methods have separate actual-core contracts and doubles here.
        text = body(source / "sun60i_a733_hdmi_phy.c")
        anchor = "\nstatic int\nphy_fail("
        if text.count(anchor) != 1:
            raise RuntimeError("PHY plan boundary changed")
        (work / "sun60i_a733_hdmi_plan.c").write_text(text.split(anchor)[0])
        binary = work / "contract"
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("A733_HDMI_NATIVE_CFLAGS", "")) +
                   ["-I" + str(work), "-I" + str(headers),
                    str(tools / "a733-hdmi-native-contract.c"),
                    "-o", str(binary)])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(binary)], check=True)


if __name__ == "__main__":
    main()
