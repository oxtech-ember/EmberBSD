#!/usr/bin/env python3
"""Check the source-level contract for the receive-disabled RAM profile."""

import argparse
from pathlib import Path
import re
import sys


ENABLED_ATTACHMENTS = re.compile(
    r"^\s*(?:ad9361phy|adiaxidmac|adiaxiadc|zynqspi)\*?\s+at\s"
)
REQUIRED_DISABLED_ATTACHMENTS = {
    "ad9361phy": ("spi", "sys/dev/spi/files.spi"),
    "adiaxidmac": ("fdt", "sys/dev/fdt/files.fdt"),
    "adiaxiadc": ("fdt", "sys/dev/fdt/files.fdt"),
    "zynqspi": ("fdt", "sys/arch/arm/xilinx/files.zynq"),
}
REQUIRED_EARLY_CONSOLE = "options EARLYCONS=zynq, CONSADDR=0xe0001000"


def active_lines(path):
    return [
        line.split("#", 1)[0].strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.split("#", 1)[0].strip()
    ]


def included_configs(source_root, start):
    pending = [start]
    visited = set()
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        for line in active_lines(path):
            match = re.fullmatch(r'include\s+"([^"]+)"', line)
            if match is None:
                continue
            include = Path(match.group(1))
            if include.name.startswith("files."):
                continue
            child = source_root / "sys" / include
            if child.is_file():
                pending.append(child)
    return sorted(visited)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path(__file__).resolve().parents[4],
    )
    args = parser.parse_args()
    source_root = args.source_root.resolve()
    config = source_root / "sys/arch/evbarm/conf/PLUTO_NANO"
    if not config.is_file():
        print(f"FAIL: missing {config}", file=sys.stderr)
        return 1

    lines = active_lines(config)
    if 'include "arch/evbarm/conf/GENERIC"' not in lines:
        print("FAIL: profile must inherit the existing Zynq-capable GENERIC", file=sys.stderr)
        return 1
    if REQUIRED_EARLY_CONSOLE not in lines:
        print("FAIL: profile must enable the Zynq UART early console", file=sys.stderr)
        return 1

    included = included_configs(source_root, config)
    for path in included:
        for line in active_lines(path):
            if ENABLED_ATTACHMENTS.match(line):
                print(f"FAIL: receive or PL attachment explicitly enabled by {path}: {line}", file=sys.stderr)
                return 1

    for device, (attachment, source) in REQUIRED_DISABLED_ATTACHMENTS.items():
        declaration = source_root / source
        pattern = re.compile(r"^attach\s+" + re.escape(device) + r"\s+at\s+" + re.escape(attachment) + r"(?:\s|$)")
        if not any(pattern.match(line) for line in active_lines(declaration)):
            print(f"FAIL: expected {device} auto-attachment in {declaration}", file=sys.stderr)
            return 1
        directive = f"no {device}* at {attachment}?"
        if directive not in lines:
            print(f"FAIL: profile must disable automatic {device} attachment with: {directive}", file=sys.stderr)
            return 1

    generic = source_root / "sys/arch/evbarm/conf/GENERIC"
    generic_lines = active_lines(generic)
    if not any(re.fullmatch(r"options\s+SOC_ZYNQ7000", line) for line in generic_lines):
        print("FAIL: GENERIC no longer selects the Zynq-7000 platform", file=sys.stderr)
        return 1
    if not any(line.startswith("zynquart*") and "at fdt?" in line for line in generic_lines):
        print("FAIL: GENERIC no longer includes the FDT Zynq UART attachment", file=sys.stderr)
        return 1

    print("PASS: PLUTO_NANO has early console and disables automatic ADI RX/PL attachments")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
