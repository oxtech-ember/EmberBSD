#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted actual-source A9 timer and UART contracts.
"""Compile production functions with a bounded register model, not hardware."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def functions(source, names):
    result = []
    for name in names:
        match = re.search(r"^([^\n]+)\n" + re.escape(name) + r"\([^;]*?\n\{.*?^}",
                          source, re.MULTILINE | re.DOTALL)
        if match is None:
            raise ValueError(f"function not found: {name}")
        result.append(match.group(0))
    return "\n\n".join(result) + "\n"


def macro(source, name):
    lines = source.splitlines()
    for index, line in enumerate(lines):
        if re.match(r"^#define\s+" + re.escape(name) + r"\(", line):
            body = [line]
            while body[-1].endswith("\\"):
                index += 1
                body.append(lines[index])
            return "\n".join(body) + "\n"
    raise ValueError(f"macro not found: {name}")


def run(args, work):
    root = Path(__file__).resolve().parents[4]
    timer = (args.timer_source or root / "sys/arch/arm/cortex/a9tmr.c").read_text(encoding="utf-8")
    uart = (args.uart_source or root / "sys/arch/arm/xilinx/zynq_uart.c").read_text(encoding="utf-8")
    tc = re.search(r"^static struct timecounter a9tmr_timecounter = \{.*?^};",
                   timer, re.MULTILINE | re.DOTALL)
    if tc is None:
        raise ValueError("timecounter initializer not found")
    (work / "timer-bodies.h").write_text(tc.group(0) + "\n" + functions(timer, [
        "a9tmr_global_read", "a9tmr_global_write", "a9tmr_attach",
        "a9tmr_gettime", "a9tmr_init_cpu_clock", "a9tmr_cpu_initclocks",
        "a9tmr_update_freq_cb", "a9tmr_update_freq", "a9tmr_get_timecount"]))
    (work / "uart-bodies.h").write_text("".join(macro(uart, name) for name in [
        "ZYNQUART_RBUF_AVAIL", "ZYNQUART_RBUF_SPACE", "ZYNQUART_RBUF_INC"])
        + functions(uart, ["zynquartintr_read", "zynquartintr"]))
    for header in ["a9tmr_reg.h", "a9tmr_var.h"]:
        (work / header).write_text((root / "sys/arch/arm/cortex" / header).read_text(encoding="utf-8"))
    (work / "zynq_uartreg.h").write_text(
        (root / "sys/arch/arm/xilinx/zynq_uartreg.h").read_text(encoding="utf-8"))
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = shlex.split(os.environ.get("CFLAGS", ""))
    # The unchanged UART receive function compares signed cc to unsigned hiwat.
    # Keep that inherited warning visible without making it a harness failure.
    for name in ("timer", "uart"):
        output = work / name
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter"] + flags +
            (["-Wno-error=sign-compare"] if name == "uart" else []) + ["-I" + str(work),
            str(Path(__file__).with_name(name + ".c")), "-o", str(output)], check=True)
        if not args.compile_only:
            subprocess.run([str(output)], check=True, timeout=10)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timer-source", type=Path)
    parser.add_argument("--uart-source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--compile-only", action="store_true")
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        run(args, args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="zynq-core-") as directory:
            run(args, Path(directory))
