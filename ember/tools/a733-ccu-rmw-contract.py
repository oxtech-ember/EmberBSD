#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted actual-source CCU RMW concurrency control.
import os
from pathlib import Path
import subprocess

root = Path(os.environ.get("CLOCK_SOURCE_ROOT", Path(__file__).resolve().parents[2]))
env = os.environ.copy()
env["CLOCK_RMW_ONLY"] = "1"
env["CLOCK_TEST_CFLAGS"] = env.get("CLOCK_TEST_CFLAGS", "") + " -DCLOCK_RMW_THREADS -pthread"
raise SystemExit(subprocess.call(["sh", str(root / "ember/tools/a733-accelerator-clock-contract.sh")], env=env))
