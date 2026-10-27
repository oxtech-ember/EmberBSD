#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
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
(include / "ic").mkdir(parents=True)
(include / "sdmmc").mkdir()
header = (root / "sys/dev/ic/rtw88var.h").read_text()
shim = "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n"
for name in ("rtw88_dma", "rtw88_bus_ops"):
    match = re.search(r"(?ms)^struct " + name + r" \{.*?^\};", header)
    if match is None:
        raise SystemExit("Missing production declaration: " + name)
    shim += match.group(0) + "\n"
(include / "ic/rtw88var.h").write_text(shim)
for path in ("ic/rtw88_io.h", "ic/rtw8723d_boot.h",
             "sdmmc/rtw88_sdio_io.h", "sdmmc/sdmmc_io_status.h",
             "sdmmc/sdmmc_ioreg.h", "sdmmc/sdmmcreg.h"):
    (include / path).write_bytes((root / "sys/dev" / path).read_bytes())
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
