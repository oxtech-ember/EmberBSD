#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
"""Extract the actual async producer and its numeric admission helpers."""
import re,sys
from pathlib import Path
root,out=map(Path,sys.argv[1:]);s=(root/"sys/dev/ic/rtw88_net.c").read_text()
names=("rtw88_net_tx_accepting_locked","rtw88_net_same_identity","rtw88_net_state_current_locked","rtw88_net_tx_current_locked","rtw88_net_tx_reserve_locked","rtw88_net_tx_policy_async","rtw88_net_tx_pending_discard","rtw88_net_start_async")
parts=[]
for name in names:
 m=re.search(r"^"+name+r"\(.*?^\}",s,re.M|re.S)
 if not m: raise SystemExit("Missing actual async producer: "+name)
 parts.append(("static void\n" if name in names[-2:] else "static int\n")+m.group())
(out/"routing-async.inc").write_text("\n".join(parts)+"\n")
