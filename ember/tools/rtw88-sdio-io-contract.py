#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the native SDIO helpers with an error-injecting card model."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
root = a.source.resolve()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
include = out / "include/dev/sdmmc"
include.mkdir(parents=True)
header = root / "sys/dev/sdmmc/rtw88_sdio_io.h"
source = root / "sys/dev/sdmmc/rtw88_sdio_io.c"
if not header.exists() or not source.exists():
    raise SystemExit("Missing native rtw88 SDIO transport implementation")
(include / header.name).write_bytes(header.read_bytes())
(include / "sdmmc_io_status.h").write_bytes(
    (root / "sys/dev/sdmmc/sdmmc_io_status.h").read_bytes())
cc = shlex.split(os.environ["HOST_CC"])
flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
         "-I" + str(out / "include")]
flags += shlex.split(os.environ.get("HOST_CFLAGS", ""))
subprocess.run(cc + flags + [str(source), str(Path(__file__).with_suffix(".c")),
                            "-o", str(out / "test")], check=True)
subprocess.run([str(out / "test")], check=True)
