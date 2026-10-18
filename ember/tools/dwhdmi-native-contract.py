#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted. Execute unchanged DW-HDMI function bodies.
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("DWHDMI_SOURCE_ROOT", tools.parent.parent))
    source = (root / "sys/dev/ic/dw_hdmi.c").read_text()
    names = ["dwhdmi_ddc_exec", "dwhdmi_read", "dwhdmi_write", "dwhdmi_vp_init",
             "dwhdmi_tx_init", "dwhdmi_cea_mode_uses_fractional_vblank",
             "dwhdmi_fc_init", "dwhdmi_mc_init", "dwhdmi_native_prepare",
             "dwhdmi_native_start", "dwhdmi_init", "dwhdmi_native_init"]
    text = source[source.index("#define\tHDMI_DESIGN_ID"):source.index("\nenum dwhdmi_dai_mixer_ctrl {")]
    for name in names:
        match = re.search(r"\n(?:static )?(?:int|uint8_t|void|bool)\n" + name + r"\(", source)
        if match is None:
            raise RuntimeError("Missing production function: " + name)
        start = source.index("{", match.start())
        depth, end = 1, start + 1
        # The selected bodies have no braces in strings or comments.
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        text += source[match.start():end] + "\n"
    with tempfile.TemporaryDirectory(prefix="dwhdmi-native-") as directory:
        work = Path(directory)
        (work / "production.c").write_text(text)
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Werror"] +
                   shlex.split(os.environ.get("DWHDMI_CFLAGS", "")) +
                   ["-I" + str(work), str(tools / "dwhdmi-native-contract.c"),
                    "-o", str(work / "test")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
