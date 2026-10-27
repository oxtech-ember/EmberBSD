#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run actual mountroot dispatcher/cancellation code with pthread barriers."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--pending-only", action="store_true")
a = p.parse_args()
root = a.source.resolve()
out = a.output.resolve()
source = (root / "sys/kern/subr_autoconf.c").read_text()
header = (root / "sys/sys/device.h").read_text()


def function(name, required=True):
    match = re.search(r"(?m)^(?:static )?(?:void|int)\n" + name + r"\(", source)
    if match is None:
        if required:
            raise SystemExit("Missing production function: " + name)
        return ""
    end = source.index("\n}", match.end()) + 2
    return source[match.start():end] + "\n"


names = ["config_mountroot", "config_process_deferred"]
if not a.pending_only:
    names += ["config_mountroot_cancel", "config_mountroot_thread"]
body = function("config_mountroot_done", required=False)
body += "\n".join(function(name) for name in names)
declaration = re.search(r"(?ms)^struct deferred_config \{.*?^\};", source)
if declaration is None:
    raise SystemExit("Missing production deferred record")
body = declaration.group(0) + "\nTAILQ_HEAD(deferred_config_head, deferred_config);\n" + body
globals_text = ""
queues = ["deferred_config_queue", "mountroot_config_queue"]
if "mountroot_config_running" in body:
    queues.append("mountroot_config_running")
for name in queues:
    match = re.search(r"(?m)^static struct deferred_config_head " + name + r" =\n[^;]+;", source)
    if match is None:
        raise SystemExit("Missing production queue: " + name)
    globals_text += match.group(0) + "\n"
prototypes = ""
for name in ["config_mountroot"] + ([] if a.pending_only else ["config_mountroot_cancel"]):
    match = re.search(r"(?m)^(?:void|int)\s+" + name + r"\([^;]+;", header)
    if match is None:
        raise SystemExit("Missing public declaration: " + name)
    prototypes += match.group(0) + "\n"
out.mkdir(parents=True, exist_ok=False)
# The actual private record precedes globals, prototypes and function bodies.
record_end = body.index("TAILQ_HEAD(deferred_config_head, deferred_config);")
record_end += len("TAILQ_HEAD(deferred_config_head, deferred_config);")
(out / "production.inc").write_text(body[:record_end] + "\n" + globals_text +
                                     prototypes + body[record_end:])
command = shlex.split(os.environ["HOST_CC"]) + [
    "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-pthread",
    "-DDIAGNOSTIC", "-I" + str(out),
] + shlex.split(os.environ.get("HOST_CFLAGS", ""))
if a.pending_only:
    command.append("-DTEST_PENDING_ONLY")
subprocess.run(command + [str(Path(__file__).with_suffix(".c")),
                          "-o", str(out / "test")], check=True)
subprocess.run([str(out / "test")], check=True, timeout=30)
