#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD RTL8723D contract tests, AI-assisted.
"""Exercise the native SDIO attachment with host services and card I/O models."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
root, out = a.source.resolve(), a.output.resolve()
source = root / "sys/dev/sdmmc/if_rtw88_sdio.c"
if not source.is_file():
    raise SystemExit("Missing native RTL8723DS attachment")
out.mkdir(parents=True, exist_ok=False)
include = out / "include/dev"
include.mkdir(parents=True)
for name in ("ic", "sdmmc"):
    (include / name).symlink_to(root / "sys/dev" / name, target_is_directory=True)
native = source.read_text()
(out / "native.inc").write_text(native[native.index("#define RTWSDIO_"):])
boot = (root / "sys/dev/ic/rtw8723d_boot.c").read_text()
match = re.search(r"(?ms)^int\nrtw8723d_fw_parse\(.*?^\}", boot)
if match is None:
    raise SystemExit("Missing production firmware parser")
parser = "\n".join(m.group(0) for m in re.finditer(
    r"(?m)^#define\s+FW_(?:HEADER_SIZE|MAX_SIZE)\s+.*", boot))
get_le16 = re.search(r"(?ms)^static uint16_t\nget_le16\(.*?^\}", boot)
if get_le16 is None:
    raise SystemExit("Missing production firmware byte decoder")
(out / "parser.inc").write_text(parser + "\n" + get_le16.group(0) + "\n" + match.group(0))
sdmmc = (root / "sys/dev/sdmmc/sdmmcvar.h").read_text()
(out / "flags.inc").write_text("\n".join(
    m.group(0) for m in re.finditer(r"(?m)^#define\s+SCF_.*", sdmmc)))
command = shlex.split(os.environ["HOST_CC"]) + [
    "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-pthread",
    "-fwrapv", "-I" + str(out), "-I" + str(out / "include"),
] + shlex.split(os.environ.get("HOST_CFLAGS", "")) + ["-UNDEBUG"]
subprocess.run(command + [str(root / "sys/dev/sdmmc/rtw88_sdio_io.c"),
                          str(Path(__file__).with_suffix(".c")),
                          "-o", str(out / "test")], check=True)
subprocess.run([str(out / "test")], check=True, timeout=60)
