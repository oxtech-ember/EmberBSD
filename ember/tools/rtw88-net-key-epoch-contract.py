#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
"""Compile the actual net key fence with the production async queue."""
import argparse, json, os, re, shlex, subprocess
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument("source",type=Path); p.add_argument("output",type=Path)
a=p.parse_args(); root=a.source.resolve(); out=a.output.resolve()
out.mkdir(parents=True,exist_ok=False)
net=(root / "sys/dev/ic/rtw88_net.c").read_text()
parts=[]
ctx=re.search(r"^struct rtw88_prepare_context \{.*?^\};",net,re.M|re.S)
if not ctx: raise SystemExit("Missing actual prepare context")
parts.append(ctx.group())
chip=(root / "sys/dev/ic/rtw88_chip.c").read_text()
prepare=re.search(r"^rtw88_prepare_station\(.*?^\}",chip,re.M|re.S)
if not prepare: raise SystemExit("Missing actual prepare station dispatcher")
parts.append("int\n"+prepare.group())
for name, prefix in (("rtw88_net_key_enter","static void"),("rtw88_net_key_leave","static void"),("rtw88_net_io_begin","int"),("rtw88_net_io_end","int"),("rtw88_net_rx_epoch","int"),("rtw88_net_prepare_locked","static int"),("rtw88_net_prepare_release_locked","static void"),("rtw88_net_same_identity","static int"),("rtw88_net_request_control","int"),("rtw88_net_control_take_locked","static int"),("rtw88_net_control_done","void"),("rtw88_net_control_current_locked","static int"),("rtw88_net_prepare_current","static int"),("rtw88_net_control_current","static int"),("rtw88_net_hardware_stop","static int"),("rtw88_net_stop_service","static void"),("rtw88_net_control_execute","static int"),("rtw88_net_control_service","static void"),("rtw88_net_state_snapshot","static int"),("rtw88_net_state_current_locked","static int"),("rtw88_net_newstate_async","static int"),("rtw88_net_state_fail","static void"),("rtw88_net_state_complete","static int"),("rtw88_net_state_async","static void"),("rtw88_net_init_async","static int"),("rtw88_net_stop_async","static void"),("rtw88_net_sta_quiet_async","static void"),("rtw88_net_fault_locked","static void"),("rtw88_net_tx_accepting_locked","static int"),("rtw88_net_tx_service","static void"),("rtw88_net_tx_policy_async","static int"),("rtw88_net_tx_current_locked","static int"),("rtw88_net_tx_reserve_locked","static int"),("rtw88_async_service","void"),("rtw88_net_shutdown_check","static int"),("rtw88_net_detach_async","static int")):
    m=re.search(r"^"+name+r"\(.*?^\}",net,re.M|re.S)
    if not m: raise SystemExit("Missing actual net owner contract: "+name)
    parts.append(prefix+"\n"+m.group())
(out / "net-key-functions.inc").write_text("\n".join(parts)+"\n")
include=out/"include/dev"; include.mkdir(parents=True)
(include/"ic").symlink_to(root/"sys/dev/ic",target_is_directory=True)
cc=shlex.split(os.environ["CC"])
cmd=cc+shlex.split(os.environ.get("RTW88_TEST_CFLAGS", ""))+["-std=c11","-Wall","-Wextra","-Werror","-UNDEBUG","-I"+str(out/"include"),"-I"+str(out),"-I"+str(root/"sys/dev/ic"),str(root/"sys/dev/ic/rtw88_async.c"),str(root/"ember/tools/rtw88-net-key-epoch-contract.c"),"-o",str(out/"test")]
fixture=str(root/"ember/tools/rtw88-net-key-epoch-contract.c")
analysis_cmd=[arg for arg in cmd if arg != str(root/"sys/dev/ic/rtw88_async.c")]
(out/"compile_commands.json").write_text(json.dumps([{"directory":str(out),"file":fixture,"arguments":analysis_cmd}],indent=2)+"\n")
subprocess.run(cmd,check=True); subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", ""))+[str(out/"test")],check=True)
print("PASS: net key fence and async queue")
