#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
"""Exercise deferred RX identity using the existing native core fixture."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
root, out = a.source.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
base = out / "hci"
subprocess.run([sys.executable, str(root / "ember/tools/rtw88-hci-contract.py"),
                str(root), str(base)], check=True)
commands = json.loads((base / "commands.json").read_text())
original = str(root / "ember/tools/rtw88-hci-contract.c")
cmd = [str(root / "ember/tools/rtw88-rx-epoch-contract.c") if x == original else x
       for x in commands[0]]
if original not in commands[0] or cmd[-2] != "-o":
    raise SystemExit("Unexpected existing HCI fixture compile command")
cmd[-1] = str(out / "test")
cmd.insert(-2, str(root / "sys/dev/ic/rtw88_rx_epoch.c"))
with (out / "compile.log").open("w") as log:
    subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
with (out / "runtime.log").open("w") as log:
    subprocess.run([str(out / "test")], stdout=log, stderr=subprocess.STDOUT, check=True)
print("PASS: RX generation/epoch propagation; final net admission requires separate checks")
