#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
"""Exercise the actual frontend stop ordering in a bounded host model."""
import os, re, shlex, subprocess, sys
from pathlib import Path
root, out = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=False)
s = (root / "sys/dev/sdmmc/if_rtw88_sdio.c").read_text()
m = re.search(r"^rtwsdio_stop\(.*?^\}", s, re.M | re.S)
if not m:
    raise SystemExit("missing actual frontend stop")
(out / "stop.inc").write_text("static int\n" + m.group() + "\n")
cmd = shlex.split(os.environ["CC"]) + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(out), str(root / "ember/tools/rtw88-sdio-stop-contract.c"), "-o", str(out / "test")]
subprocess.run(cmd, check=True)
subprocess.run([str(out / "test")], check=True)
print("PASS: frontend publication exclusion and detach order model")
