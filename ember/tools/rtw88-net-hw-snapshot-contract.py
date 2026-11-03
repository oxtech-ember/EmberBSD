#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
"""Exercise the real net numeric snapshot against the real softc definition."""
import argparse, os, re, shlex, subprocess, sys
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument("source",type=Path); p.add_argument("output",type=Path)
a=p.parse_args(); root=a.source.resolve(); out=a.output.resolve(); out.mkdir(parents=True,exist_ok=False)
net=(root/"sys/dev/ic/rtw88_net.c").read_text()
for name, pattern, prefix in (("net-hw-layout.inc",r"^struct rtw88_net_hw_snapshot \{.*?^\};", ""), ("net-hw-capture.inc",r"^rtw88_net_hw_capture\(.*?^\}","static void\n"), ("net-hw-update.inc",r"^rtw88_net_hw_update\(.*?^\}","void\n")):
 m=re.search(pattern,net,re.M|re.S)
 if not m: raise SystemExit("Missing actual numeric snapshot: "+name)
 (out/name).write_text(prefix+m.group()+"\n")
subprocess.run([sys.executable,str(root/"ember/tools/rtw88-state-contract.py"),str(root),str(out)],check=True)
inc=out/"include"; (inc/"dev").mkdir(parents=True); (inc/"dev/ic").symlink_to(root/"sys/dev/ic",target_is_directory=True)
cmd=shlex.split(os.environ["CC"])+["-std=c11","-Wall","-Wextra","-Werror","-include",str(root/"ember/tools/rtw88-test-kernel.h"),"-I"+str(inc),"-I"+str(out),"-I"+str(root/"ember/tools"),str(root/"ember/tools/rtw88-net-hw-snapshot-contract.c"),"-o",str(out/"test")]
subprocess.run(cmd,check=True); subprocess.run([str(out/"test")],check=True)
print("PASS: PCI/SDIO numeric power snapshot")
