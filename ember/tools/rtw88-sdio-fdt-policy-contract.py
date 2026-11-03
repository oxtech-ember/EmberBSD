#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D FDT policy contract, AI-assisted.
"""Exercise actual FDT policy parsing and shared admission with a host model."""
import os, re, shlex, subprocess, sys
from pathlib import Path
root, out = map(Path, sys.argv[1:]); out.mkdir(parents=True, exist_ok=False)
s = (root / "sys/dev/sdmmc/if_rtw88_sdio.c").read_text()
m = re.search(r"^rtwsdio_fdt_profile\(.*?^\}", s, re.M | re.S)
if not m: raise SystemExit("missing FDT policy reader")
n = re.search(r"^rtwsdio_profile\(.*?^\}", s, re.M | re.S)
if not n: raise SystemExit("missing profile arbitration")
(out / "profile-fdt.inc").write_text("static int\n" + m.group() + "\nstatic int\n" + n.group() + "\n")
dev = out / "include/dev"; dev.mkdir(parents=True)
(dev / "ic").symlink_to(root / "sys/dev/ic", target_is_directory=True)
cmd=shlex.split(os.environ["CC"])+["-std=c11","-Wall","-Wextra","-Werror","-UNDEBUG","-DFDT","-I"+str(out/"include"),"-I"+str(out),str(root/"sys/dev/ic/rtw88_profile.c"),str(root/"ember/tools/rtw88-sdio-fdt-policy-contract.c"),"-o",str(out/"test")]
subprocess.run(cmd,check=True); subprocess.run([str(out/"test")],check=True)
print("PASS: explicit FDT policy and shared admission")
