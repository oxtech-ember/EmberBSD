#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD firmware width-order regression, AI-assisted (OpenAI Codex).
"""Build a userland fixture from the actual driver bodies and wire types."""
import argparse
from pathlib import Path
import re


def declaration(text, pattern):
    """Extract one brace-delimited declaration, preserving the source body."""
    start = re.search(pattern, text, re.M)
    if start is None:
        raise ValueError(f"declaration not found: {pattern}")
    opening = text.index("{", start.start())
    # Ignore braces in comments and literals while finding the matching brace.
    tokens = re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
                         r"'(?:\\.|[^'\\])*'|[{}]", text[opening:], re.S)
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start.start():opening + token.end()]
    raise ValueError("unterminated declaration")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True,
                        help="if_iwx.c with its sibling if_iwxreg.h")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    driver = args.source.read_text()
    registers = args.source.with_name("if_iwxreg.h").read_text()
    bodies = [declaration(driver, rf"^int\n{name}\(") for name in
              ("iwx_rs_init_v3", "iwx_rs_init_v4", "iwx_rs_init", "iwx_run_stop")]
    wire = [declaration(registers, rf"^enum {name} \{{") + ";" for name in
            ("iwx_tlc_mng_cfg_cw", "iwx_tlc_mng_cfg_mode")]
    wire.extend(re.findall(r"^#define IWX_TLC_(?:NSS_|MCS_PER_BW_|MNG_CHAIN_)"
                           r"[^\n]+", registers, re.M))
    wire.extend(declaration(registers, rf"^struct {name} \{{") + " __packed;"
                for name in ("iwx_tlc_config_cmd_v3", "iwx_tlc_config_cmd_v4"))
    template = Path(__file__).with_suffix(".c").read_text()
    args.output.write_text(template.replace("/* WIRE_TYPES */", "\n".join(wire))
                           .replace("/* DRIVER_FUNCTIONS */", "\n\n".join(bodies)))


if __name__ == "__main__":
    main()
