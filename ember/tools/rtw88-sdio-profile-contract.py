#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
"""Check actual explicit-profile routing with a property dictionary model."""
import os, re, shlex, subprocess, sys
from pathlib import Path
root,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=False)
s=(root/"sys/dev/sdmmc/if_rtw88_sdio.c").read_text()
m=re.search(r"^rtwsdio_has_radio_profile\(.*?^\}",s,re.M|re.S)
if not m: raise SystemExit("missing profile route predicate")
(out/"profile.inc").write_text("static bool\n"+m.group()+"\n")
subprocess.run(shlex.split(os.environ["CC"])+["-std=c11","-Wall","-Wextra","-Werror","-I"+str(out),str(root/"ember/tools/rtw88-sdio-profile-contract.c"),"-o",str(out/"test")],check=True)
subprocess.run([str(out/"test")],check=True)
print("PASS: absent and explicit radio-profile routing")
