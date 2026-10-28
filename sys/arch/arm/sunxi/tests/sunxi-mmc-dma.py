#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted actual-source SD/MMC DMA regression.
"""Exercise real DMA preparation; no device or physical DMA access occurs."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess


def span(source, start, end):
    first = source.index(start)
    last = source.index(end, first)
    return source[first:last]

def assemble(source):
    timing = span(source, "enum sunxi_mmc_timing {", "#define SUNXI_MMC_NDESC")
    config_type = span(source, "struct sunxi_mmc_config {", "\n};") + "\n};\n"
    configs = span(source,
        "static const struct sunxi_mmc_config sun4i_a10_mmc_config =",
        "static const struct device_compatible_entry compat_data[] =")
    compatible = span(source,
        "static const struct device_compatible_entry compat_data[] = {",
        "\n};") + "\n};\n"
    dma = span(source,
        "static int\nsunxi_mmc_dma_prepare(",
        "\nstatic void\nsunxi_mmc_dma_complete(")
    if dma.count("sunxi_mmc_dma_prepare") != 1:
        raise SystemExit("expected exactly one complete DMA prepare function")
    return timing, config_type, configs, compatible, dma

def preamble():
    return r'''#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int u_int;
#define __BIT(n) (1U << (n))
#define __BITS(hi, lo) (((~0U) >> (31U - (hi))) & (~0U << (lo)))
#define __packed __attribute__((__packed__))
#include "sunxi_mmc.h"

typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef struct fake_registers *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef void *bus_dma_tag_t;
struct sunxi_mmc_config;
struct bus_dma_segment { bus_addr_t ds_addr; bus_size_t ds_len; };
struct bus_dmamap { int dm_nsegs; struct bus_dma_segment dm_segs[4]; };
typedef struct bus_dmamap *bus_dmamap_t;
struct device_compatible_entry {
	const char *compat;
	const void *data;
};
#define DEVICE_COMPAT_EOL { .compat = NULL, .data = NULL }

struct fake_registers {
	uint32_t words[64];
	unsigned int reads;
	unsigned int writes;
	uint32_t last_write_reg;
	uint32_t last_write_value;
};

struct sdmmc_command {
	bus_dmamap_t c_dmamap;
	void *c_data;
	size_t c_datalen;
	int c_resid;
	int c_flags;
};
#define SCF_CMD_READ 0x01
#define ISSET(v, f) (((v) & (f)) != 0)
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_PREWRITE 2
#define BUS_DMASYNC_POSTREAD 3
#define BUS_DMASYNC_POSTWRITE 4
#define uimin(a, b) ((a) < (bus_size_t)(b) ? (a) : (bus_size_t)(b))
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define htole32(v) __builtin_bswap32((uint32_t)(v))
#else
#define htole32(v) ((uint32_t)(v))
#endif

static unsigned int sync_calls;
static void
bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map, bus_size_t offset,
    bus_size_t length, int operation)
{
	(void)tag;
	(void)map;
	(void)offset;
	(void)length;
	(void)operation;
	sync_calls++;
}

static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t reg, uint32_t value)
{
	(void)handle;
	if (reg / sizeof(uint32_t) < 64)
		tag->words[reg / sizeof(uint32_t)] = value;
	tag->writes++;
	tag->last_write_reg = (uint32_t)reg;
	tag->last_write_value = value;
}

static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t reg)
{
	(void)handle;
	tag->reads++;
	if (reg / sizeof(uint32_t) < 64)
		return tag->words[reg / sizeof(uint32_t)];
	return 0;
}

static void
aprint_error_dev(void *device, const char *format, ...)
{
	(void)device;
	(void)format;
}

struct sunxi_mmc_softc {
	void *sc_dev;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	bus_dma_tag_t sc_dmat;
	const struct sunxi_mmc_config *sc_config;
	bus_dmamap_t sc_idma_map;
	int sc_idma_ndesc;
	void *sc_idma_desc;
	bus_dmamap_t sc_dmabounce_map;
	void *sc_dmabounce_buf;
	size_t sc_dmabounce_buflen;
	bus_size_t sc_idma_size;
};

#define MMC_WRITE(sc, reg, val) \
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))
#define MMC_READ(sc, reg) \
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("HOST_CC"),
                        help="Explicit absolute native compiler path")
    parser.add_argument("--cflags", default=os.environ.get("HOST_CFLAGS", ""))
    parser.add_argument("--source", type=Path,
                        help="Override the production sunxi_mmc.c input")
    parser.add_argument("--build-dir", required=True, type=Path,
                        help="Fresh output directory owned by this run")
    args = parser.parse_args()
    if not args.cc or not Path(args.cc).is_absolute():
        parser.error("set HOST_CC or --cc to the selected absolute compiler path")
    owner = Path(__file__).resolve().parent.parent
    source = args.source or owner / "sunxi_mmc.c"
    text = source.read_text()
    # Preserve the production BSD notice in the generated translation unit.
    notice = text[:text.index('#include "opt_sunximmc.h"')]
    tests = Path(__file__).with_name("sunxi-mmc-dma.inc").read_text()
    generated_text = notice + preamble() + "\n".join(assemble(text)) + "\n" + tests
    args.build_dir.mkdir(parents=True, exist_ok=False)
    generated = args.build_dir / "sunxi-mmc-dma-test.c"
    binary = args.build_dir / "sunxi-mmc-dma-test"
    generated.write_text(generated_text)
    command = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror"]
    command += shlex.split(args.cflags)
    command += ["-I", str(source.parent), str(generated), "-o", str(binary)]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=10)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
