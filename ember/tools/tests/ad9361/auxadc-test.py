#!/usr/bin/env python3
#
# Copyright (c) 2026 EmberBSD contributors.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
# TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the actual AUXADC accessor with deterministic SPI failures."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_tree", type=Path)
    parser.add_argument("--source-file", type=Path,
                        help="Alternative AD9361 source for a causal baseline check")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--sanitize", action="store_true")
    mode.add_argument("--memcheck", action="store_true")
    mode.add_argument("--analyze", action="store_true")
    args = parser.parse_args()
    driver = args.source_tree / "sys/external/bsd/ad9361/dist"
    source = args.source_file or driver / "ad9361_noos.c"
    function = re.search(r"^int32_t ad9361_get_auxadc\([^\n]+\n\{\n.*?^\}",
                         source.read_text(), re.MULTILINE | re.DOTALL)
    if function is None:
        parser.error("The AUXADC accessor was not found in the selected source.")
    registers = driver / "ad9361.h"
    names = ("REG_AUXADC_CONFIG", "AUXADC_POWER_DOWN", "REG_AUXADC_LSB", "AUXADC_WORD_LSB")
    definitions = []
    for name in names:
        lines = [line for line in registers.read_text().splitlines()
                 if re.match(r"^#define\s+" + name + r"[\s(]", line)]
        if len(lines) != 1:
            parser.error("Expected one register definition: " + name)
        definitions += lines
    with tempfile.TemporaryDirectory(prefix="ad9361-auxadc-") as temporary:
        work = Path(temporary)
        (work / "auxadc.inc").write_text(function.group(0) + "\n")
        (work / "registers.h").write_text("\n".join(definitions) + "\n")
        test = Path(__file__).with_suffix(".c")
        cc = os.environ.get("CC", "cc")
        command = [cc, "-std=c11", "-O0", "-g", "-Wall", "-Wextra", "-Werror", "-I" + str(work)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
        if args.analyze:
            command += ["-fanalyzer"]
        subprocess.run(command + [str(test), "-o", str(work / "test")], check=True)
        if args.analyze:
            subprocess.run(["cppcheck", "--enable=warning,style,performance,portability",
                "--error-exitcode=1", "--suppress=missingIncludeSystem", "-I" + str(work), str(test)], check=True)
        run = [str(work / "test")]
        if args.memcheck:
            run = ["valgrind", "--leak-check=full", "--show-leak-kinds=all",
                "--errors-for-leak-kinds=all", "--error-exitcode=1", "--track-origins=yes"] + run
        subprocess.run(run, check=True)


if __name__ == "__main__":
    main()
