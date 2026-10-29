#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check the native iSCSI consumers select the common source."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
for name in ("make", "source", "tooldir"):
    p.add_argument("--" + name, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
for directory in ("lib", "initiator", "target"):
    base = [a.make, "-r", "-m", str(root / "share/mk"), "-C",
            str(root / "external/bsd/iscsi" / directory), "MAKECONF=/dev/null",
            "_SRC_TOP_=", "NETBSDSRCDIR=" + str(root), "MACHINE=evbarm",
            "MACHINE_ARCH=aarch64", "USETOOLS=yes", "TOOLDIR=" + a.tooldir]
    def run(*args):
        return subprocess.run([*base, *args], capture_output=True, text=True)
    assert run("-n", "obj").returncode == 0, directory
    include = run("-n", "includes")
    assert (include.returncode != 0) == (directory == "lib"), directory
    for goal in ("all", "dependall", "install"):
        result = run("-n", goal)
        assert result.returncode != 0 and "Set EMBER_ISCSI_SOURCE" in result.stderr, (directory, goal)
    result = run("EMBER_ISCSI_SOURCE=" + a.source, "-V", "${ISCSIDIST}")
    assert result.returncode == 0 and result.stdout.strip() == a.source, directory
    assert run("EMBER_ISCSI_SOURCE=/missing/iscsi", "-V", "${ISCSIDIST}").returncode != 0
print("iSCSI source routing: PASS")
