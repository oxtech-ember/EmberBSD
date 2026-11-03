#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
"""Exercise the actual SDIO F1/common-CIS selection with a bounded model."""
import os,re,shlex,subprocess,sys
from pathlib import Path
root,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=False)
s=(root/"sys/dev/sdmmc/if_rtw88_sdio.c").read_text()
m=re.search(r"^rtwsdio_match\(.*?^\}",s,re.M|re.S)
if not m: raise SystemExit("missing actual SDIO match")
(out/"sdio-match.inc").write_text("static int\n"+m.group()+"\n")
cmd=shlex.split(os.environ["CC"])+["-std=c11","-Wall","-Wextra","-Werror","-I"+str(out),str(root/"ember/tools/rtw88-sdio-match-contract.c"),"-o",str(out/"test")]
subprocess.run(cmd,check=True);subprocess.run([str(out/"test")],check=True)
print("PASS: SDIO function/common CIS match")
