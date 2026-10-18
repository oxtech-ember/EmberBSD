#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted; native DE consumer and fault controls.
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def main():
    tools = Path(__file__).resolve().parent
    root = Path(os.environ.get("DE352_SOURCE_ROOT", tools.parent.parent))
    with tempfile.TemporaryDirectory(prefix="a733-de352-native-") as directory:
        work = Path(directory)
        ccu = (root / "sys/arch/arm/sunxi/sun60i_a733_ccu.h").read_text()
        enums = re.findall(r"^enum sun60i_a733_display_memory_reg \{.*?^};", ccu, re.M | re.S)
        if len(enums) != 1:
            raise RuntimeError("Expected the public DISPLAY memory register enum")
        (work / "display-memory-regs.h").write_text(enums[0] + "\n")
        for stem in ("sun60i_a733_de352", "sun60i_a733_de352_dma"):
            for suffix in ("c", "h"):
                name = f"{stem}.{suffix}"
                source = (root / "sys/arch/arm/sunxi" / name).read_text()
                text = "\n".join(line for line in source.splitlines()
                                 if not line.startswith(("#include ", "__KERNEL_RCSID(")))
                (work / name).write_text(text + "\n")
        source = (root / "sys/arch/arm/sunxi/sun60i_a733_de352_fdt.c").read_text()
        text = "\n".join(line for line in source.splitlines()
                         if not line.startswith(("#include ", "__KERNEL_RCSID(")))
        (work / "sun60i_a733_de352_fdt.c").write_text(text + "\n")
        for name, directory in (("sun60i_a733_output.h", "sys/arch/arm/sunxi"),
                                ("a733-de352-dma-contract.c", "ember/tools")):
            (work / name).write_bytes((root / directory / name).read_bytes())
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("DE352_CFLAGS", "")) +
                   [f"-I{work}", str(tools / "a733-de352-native-contract.c"),
                    "-o", str(work / "test")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                       [str(work / "test")], check=True)


if __name__ == "__main__":
    main()
