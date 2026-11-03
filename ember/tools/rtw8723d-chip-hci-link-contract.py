#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
"""Link the native 8723D chip and SDIO HCI with a stubbed actor boundary."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
root, out = a.source.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
subprocess.run([sys.executable, str(root / "ember/tools/rtw88-state-contract.py"),
                str(root), str(out)], check=True)
include = out / "include"
(include / "sys").mkdir(parents=True)
(include / "dev").mkdir()
for name in ("ic", "sdmmc"):
    (include / "dev" / name).symlink_to(root / "sys/dev" / name, target_is_directory=True)
for name in ("param.h", "systm.h"):
    (include / "sys" / name).write_text(
        '#include <errno.h>\n#include "rtw88-test-kernel.h"\n', encoding="utf-8")
ic, sd = root / "sys/dev/ic", root / "sys/dev/sdmmc"
common = ("rtw88", "rtw88_chip", "rtw88_hw", "rtw88_fw_init", "rtw88_net_policy",
          "rtw88_profile", "rtw88_phy_table", "rtw88_async", "rtw88_rx_epoch", "rtw8723d")
files = [ic / (name + ".c") for name in common]
files += sorted(ic.glob("rtw8723d_*.c")) + sorted(sd.glob("rtw88_sdio_*.c"))
files += [root / "ember/tools/rtw8723d-chip-hci-link-contract.c"]
cc = shlex.split(os.environ.get("CC", ""))
if not cc:
    raise SystemExit("Select the SDK compiler through CC")
cmd = cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-D__unused=__attribute__((unused))",
            "-DRTW88_START_CORE_ONLY", "-include", str(root / "ember/tools/rtw88-test-kernel.h"),
            "-I" + str(include), "-I" + str(out), "-I" + str(root / "ember/tools")]
cmd += list(map(str, files)) + ["-o", str(out / "test")]
(out / "commands.json").write_text(json.dumps([cmd], indent=2) + "\n", encoding="utf-8")
with (out / "compile.log").open("w") as log:
    subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
subprocess.run([str(out / "test")], check=True)
print("PASS: 8723D chip and SDIO HCI host link (executor stubbed; no netif)")
