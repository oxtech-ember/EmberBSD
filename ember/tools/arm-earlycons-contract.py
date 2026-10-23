#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Exercise actual early console source.
"""Check the 32-bit 16550 helper, platform selectors and boot flags."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def function(text, name):
    match = re.search(r"(?:void(?: __noasan)?\n)" + name + r"\([^;]*?\)\n\{", text)
    if not match:
        raise ValueError("Function not found: " + name)
    pos = match.end()
    depth = 1
    while depth:
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
        pos += 1
    return text[match.start():pos]


def emit(source, work):
    (work / "sys").mkdir(parents=True)
    (work / "dev/ic").mkdir(parents=True)
    for name in ("com_early.h", "comreg.h", "ns16550reg.h"):
        shutil.copyfile(source / "sys/dev/ic" / name, work / "dev/ic" / name)
    for name in ("boot_flag.h", "reboot.h"):
        shutil.copyfile(source / "sys/sys" / name, work / "sys" / name)
    (work / "sys/endian.h").write_text('''/* Instrument endian conversion at the host MMIO boundary. */
#define le32toh(v) poll_read(v)
#define htole32(v) transmit_word(v)
''')
    arm = (source / "sys/arch/arm/fdt/arm_platform.c").read_text()
    sunxi = (source / "sys/arch/arm/sunxi/sunxi_platform.c").read_text()
    macros = re.search(r"^#define\s+ARM_PTOV.*$", arm, re.M).group(0)
    platform = (source / "sys/arch/arm/sunxi/sunxi_platform.h").read_text()
    macros += "\n" + "\n".join(line for line in platform.splitlines()
                                if re.match(r"#define\s+SUNXI_CORE_[PV]BASE", line))
    selectors = (function(arm, "com32_platform_early_putchar") + "\n" +
                 function(sunxi, "sunxi_platform_early_putchar"))
    parser = function((source / "sys/arch/aarch64/aarch64/aarch64_machdep.c").read_text(),
                      "parse_mi_bootargs")
    fixture = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#define __noasan
static unsigned checks, reads, ready_after, writes;
static uint32_t expected_word;
#define CHECK(v) do { checks++; if (!(v)) { \
    fprintf(stderr, "check failed at %d: %s\n", __LINE__, #v); exit(1); } } while (0)
static uint32_t
poll_read(uint32_t value)
{
    CHECK(value == expected_word);
    reads++;
    CHECK(reads <= ready_after);
    return reads < ready_after ? (value & ~UINT32_C(0x20)) : value;
}
static uint32_t
transmit_word(uint32_t value)
{
    CHECK(reads == ready_after);
    CHECK(value <= 255);
    writes++;
    return value;
}
#include <dev/ic/com_early.h>
#include <sys/boot_flag.h>
static unsigned boothowto;
PARSER

typedef uintptr_t bus_addr_t;
#define KERNEL_IO_VBASE UINT64_C(0xffff000080000000)
#define DEVMAP_ALIGN(p) ((p) & ~UINT64_C(0xfff))
MACROS
static bool use_va;
static bool cpu_earlydevice_va_p(void) { return use_va; }
static uintptr_t recorded_address;
static unsigned calls;
static char recorded_char;
static void
record_putchar(const volatile uint32_t *address, char c)
{
    recorded_address = (uintptr_t)address;
    recorded_char = c;
    calls++;
}
#define com_early32_putchar record_putchar
#define CONSADDR UINT64_C(0x02500000)
SELECTORS
#undef CONSADDR
#undef CONSADDR_VA
#define com32_platform_early_putchar com32_offset_putchar
#define sunxi_platform_early_putchar sunxi_offset_putchar
#define CONSADDR UINT64_C(0x02500300)
SELECTORS
#undef com32_platform_early_putchar
#undef sunxi_platform_early_putchar
#undef CONSADDR
#undef CONSADDR_VA
#define com32_platform_early_putchar com32_disabled_putchar
#define sunxi_platform_early_putchar sunxi_disabled_putchar
DISABLED_SELECTORS
#undef com32_platform_early_putchar
#undef sunxi_platform_early_putchar
#undef com_early32_putchar

int
main(void)
{
    volatile uint32_t words[8];
    for (unsigned delay = 1; delay <= 3; delay++) {
        for (unsigned c = 0; c <= 255; c++) {
            for (unsigned i = 0; i < 8; i++)
                words[i] = 0xaabb0000 + i;
            /* Only the LSR_TXRDY bit permits a write, even with other bits. */
            expected_word = LSR_TXRDY | 0x80;
            words[com_lsr] = expected_word;
            reads = writes = 0;
            ready_after = delay;
            com_early32_putchar(words, (char)c);
            CHECK(writes == 1 && reads == delay);
            CHECK(words[com_data] == c);
            for (unsigned i = 1; i < 8; i++)
                CHECK(words[i] == (i == com_lsr ? expected_word : 0xaabb0000 + i));
        }
    }
    for (unsigned va = 0; va < 2; va++) {
        use_va = va;
        unsigned before = calls;
        com32_platform_early_putchar('A');
        CHECK(calls == before + 1 && recorded_char == 'A');
        CHECK(recorded_address == (va ? KERNEL_IO_VBASE : 0x02500000));
        sunxi_platform_early_putchar('B');
        CHECK(calls == before + 2 && recorded_char == 'B');
        CHECK(recorded_address == (va ? KERNEL_IO_VBASE + 0x900000 : 0x02500000));
        com32_offset_putchar('C');
        CHECK(recorded_address == (va ? KERNEL_IO_VBASE + 0x300 : 0x02500300));
        sunxi_offset_putchar('D');
        CHECK(recorded_address == (va ? KERNEL_IO_VBASE + 0x900300 : 0x02500300));
        CHECK(calls == before + 4);
        com32_disabled_putchar('X');
        sunxi_disabled_putchar('Y');
        CHECK(calls == before + 4);
    }
    char configured[] = "-s";
    char loader[] = "root=ld0a console=serial -v";
    char unknown[] = "unrecognized -?";
    boothowto = 0;
    parse_mi_bootargs(configured);
    CHECK(boothowto == RB_SINGLE);
    parse_mi_bootargs(loader);
    CHECK(boothowto == (RB_SINGLE | AB_VERBOSE));
    parse_mi_bootargs(unknown);
    CHECK(boothowto == (RB_SINGLE | AB_VERBOSE));
    printf("PASS: %u actual-source early-console and boot-flag checks\n", checks);
    return 0;
}
'''
    # With no CONSADDR, the production selector intentionally ignores c.
    # Annotate only these two opt-out fixture parameters, not the whole TU.
    disabled = selectors.replace("(char c)", "(char c __attribute__((unused)))")
    fixture = (fixture.replace("PARSER", parser).replace("MACROS", macros)
               .replace("DISABLED_SELECTORS", disabled).replace("SELECTORS", selectors))
    (work / "contract.c").write_text(fixture)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--emit", type=Path)
    args = parser.parse_args()
    if args.emit:
        emit(args.source_root, args.emit)
        return
    with tempfile.TemporaryDirectory(prefix="arm-earlycons-") as tmp:
        work = Path(tmp)
        emit(args.source_root, work)
        command = (shlex.split(os.environ.get("CC", "cc")) +
                   ["-std=c11", "-Wall", "-Wextra", "-Werror"] +
                   shlex.split(os.environ.get("EARLYCONS_CFLAGS", "")) +
                   ["-I" + str(work), str(work / "contract.c"), "-o", str(work / "contract")])
        subprocess.run(command, check=True)
        subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) + [str(work / "contract")],
                       check=True, timeout=30)


if __name__ == "__main__":
    main()
