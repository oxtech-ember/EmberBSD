#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise production generic and S31 erase functions with mock devices."""

import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

root = pathlib.Path(__file__).resolve().parents[4]


def extract(path, pattern):
    text = path.read_text()
    match = re.search(pattern, text)
    assert match, f"missing production function in {path}: {pattern}"
    depth = 1
    end = match.end()
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[match.start():end]


generic_erase = extract(root / "sys/dev/flash/flash.c",
                        r"int\nflash_erase\(device_t self, struct flash_erase_instruction \* const ei\)\n\{")
s31_erase = extract(root / "sys/arch/riscv/espressif/esp32s31_flash.c",
                    r"static int\nesp32s31_flash_erase\(device_t self, struct flash_erase_instruction \*ei\)\n\{")
s31_read = extract(root / "sys/arch/riscv/espressif/esp32s31_flash.c",
                   r"static int\nesp32s31_flash_read\(device_t self, flash_off_t offset, size_t length,\n\tsize_t \*retlen, uint8_t \*buffer\)\n\{")
s31_write = extract(root / "sys/arch/riscv/espressif/esp32s31_flash.c",
                    r"static int\nesp32s31_flash_write\(device_t self, flash_off_t offset, size_t length,\n\tsize_t \*retlen, const uint8_t \*buffer\)\n\{")

fixture = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct device *device_t;
typedef int64_t flash_off_t;
typedef uint64_t flash_size_t;
struct flash_erase_instruction {
    flash_off_t ei_addr;
    flash_off_t ei_len;
    void (*ei_callback)(struct flash_erase_instruction *);
    unsigned long ei_priv;
    unsigned char ei_state;
};
enum { FLASH_ERASE_DONE = 0, FLASH_ERASE_FAILED = 1 };
struct flash_partition {
    flash_off_t part_offset;
    flash_size_t part_size;
};
struct flash_interface {
    int (*erase)(device_t, struct flash_erase_instruction *);
};
struct flash_softc {
    struct flash_interface *flash_if;
    struct flash_partition sc_partinfo;
    bool sc_readonly;
};
struct device { void *private; device_t parent; };
#define device_private(d) ((d)->private)
#define device_parent(d) ((d)->parent)
#define KASSERT(c) assert(c)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define S31_FLASH_MAX_TRANSFER 4096
#define S31_SBI_FLASH_ERASE 9
#define S31_SBI_FLASH_READ 7
#define S31_SBI_FLASH_WRITE 8
#define S31_FLASH_WRITE_ALIGN 4
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_POSTREAD 2
#define BUS_DMASYNC_PREWRITE 4
#define BUS_DMASYNC_POSTWRITE 8

static int generic_error;
static bool generic_leave_state;
static unsigned generic_calls;
static flash_off_t generic_seen_addr, generic_seen_len;
static device_t generic_seen_device;
static int mock_generic_erase(device_t dev, struct flash_erase_instruction *ei)
{
    generic_calls++;
    generic_seen_device = dev;
    generic_seen_addr = ei->ei_addr;
    generic_seen_len = ei->ei_len;
    if (!generic_leave_state)
        ei->ei_state = generic_error ? FLASH_ERASE_FAILED : FLASH_ERASE_DONE;
    return generic_error;
}

static bool s31_locked;
static int s31_error;
static unsigned s31_calls, s31_callbacks;
static unsigned s31_dma_syncs;
struct esp32s31_flash_softc;
static void mutex_enter(void *lock) { (void)lock; assert(!s31_locked); s31_locked = true; }
static void mutex_exit(void *lock) { (void)lock; assert(s31_locked); s31_locked = false; }

struct esp32s31_flash_softc {
    struct { flash_off_t erasesize; } sc_if;
    flash_size_t sc_size;
    int sc_lock;
    void *sc_buf;
};
static void esp32s31_flash_dma_sync(struct esp32s31_flash_softc *sc,
    size_t length, int ops)
{
    (void)sc; (void)length; (void)ops;
    s31_dma_syncs++;
}
static int esp32s31_flash_call(struct esp32s31_flash_softc *sc, long func,
    uint32_t offset, uint32_t length)
{
    (void)sc; (void)func; (void)offset; (void)length;
    s31_calls++;
    return s31_error;
}
static void s31_done_callback(struct flash_erase_instruction *ei)
{
    assert(!s31_locked);
    assert(ei->ei_state == FLASH_ERASE_DONE);
    s31_callbacks++;
}

""" + generic_erase + s31_read + s31_write + s31_erase + r"""
int main(void)
{
    struct device parent = {0};
    struct flash_interface fi = { .erase = mock_generic_erase };
    struct flash_softc fsc = { .flash_if = &fi,
        .sc_partinfo = { .part_offset = 0x1000, .part_size = 0x3000 } };
    struct device flash = { .private = &fsc, .parent = &parent };
    struct flash_erase_instruction ei = { .ei_addr = 0x200, .ei_len = 0x400,
        .ei_state = FLASH_ERASE_FAILED };

    assert(flash_erase(&flash, &ei) == 0);
    assert(generic_calls == 1 && generic_seen_device == &parent);
    assert(generic_seen_addr == 0x1200 && generic_seen_len == 0x400);
    assert(ei.ei_addr == 0x200 && ei.ei_len == 0x400);
    assert(ei.ei_state == FLASH_ERASE_DONE);

    generic_error = EIO;
    ei.ei_state = FLASH_ERASE_DONE;
    assert(flash_erase(&flash, &ei) == EIO);
    assert(ei.ei_addr == 0x200 && ei.ei_len == 0x400);
    assert(ei.ei_state == FLASH_ERASE_FAILED);

    /* A parent that leaves ei_state untouched cannot claim success. */
    generic_error = 0;
    generic_leave_state = true;
    ei.ei_state = FLASH_ERASE_DONE;
    assert(flash_erase(&flash, &ei) == 0);
    assert(ei.ei_state == FLASH_ERASE_FAILED);
    generic_leave_state = false;
    generic_error = EIO;
    assert(generic_calls == 3);

    fsc.sc_readonly = true;
    ei.ei_state = FLASH_ERASE_DONE;
    assert(flash_erase(&flash, &ei) == EACCES);
    assert(ei.ei_state == FLASH_ERASE_FAILED && generic_calls == 3);

    fsc.sc_readonly = false;
    generic_error = 0;
    ei.ei_addr = -1;
    ei.ei_len = 0x400;
    ei.ei_state = FLASH_ERASE_DONE;
    assert(flash_erase(&flash, &ei) == EINVAL);
    assert(ei.ei_state == FLASH_ERASE_FAILED && generic_calls == 3);
    ei.ei_addr = 0;
    ei.ei_len = -1;
    assert(flash_erase(&flash, &ei) == EINVAL);
    assert(generic_calls == 3);
    ei.ei_addr = 0x3000;
    ei.ei_len = 1;
    assert(flash_erase(&flash, &ei) == EINVAL);
    assert(generic_calls == 3);
    ei.ei_addr = 0x200;
    ei.ei_len = 0x400;
    fsc.sc_partinfo.part_offset = -1;
    assert(flash_erase(&flash, &ei) == EINVAL);
    assert(generic_calls == 3);
    fsc.sc_partinfo.part_offset = INT64_MAX - 0x1ff;
    assert(flash_erase(&flash, &ei) == EINVAL);
    assert(generic_calls == 3);

    /* Physical end must remain representable even if the relative range fits. */
    fsc.sc_partinfo.part_offset = 1;
    fsc.sc_partinfo.part_size = INT64_MAX;
    ei.ei_addr = 0;
    ei.ei_len = INT64_MAX;
    assert(flash_erase(&flash, &ei) == EINVAL);
    assert(ei.ei_state == FLASH_ERASE_FAILED && generic_calls == 3);

    ei.ei_addr = 0x200;
    ei.ei_len = 0x400;
    fsc.sc_partinfo.part_offset = 0x1000;
    fsc.sc_partinfo.part_size = (flash_size_t)INT64_MAX + 1;
    assert(flash_erase(&flash, &ei) == EINVAL);
    assert(generic_calls == 3);
    fsc.sc_partinfo.part_size = 0x3000;

    {
        struct esp32s31_flash_softc ssc = { .sc_if = { .erasesize = 4096 },
            .sc_size = 8192 };
        struct device s31 = { .private = &ssc };
        uint8_t buffer[8] = {0};
        size_t retlen = 99;
        struct flash_erase_instruction se = { .ei_addr = 4096,
            .ei_len = 4096, .ei_callback = s31_done_callback,
            .ei_state = FLASH_ERASE_FAILED };

        assert(esp32s31_flash_erase(&s31, &se) == 0);
        assert(se.ei_state == FLASH_ERASE_DONE);
        assert(s31_calls == 1 && s31_callbacks == 1 && !s31_locked);
        s31_error = EIO;
        se.ei_addr = 0;
        se.ei_state = FLASH_ERASE_DONE;
        assert(esp32s31_flash_erase(&s31, &se) == EIO);
        assert(se.ei_state == FLASH_ERASE_FAILED);
        assert(s31_calls == 2 && s31_callbacks == 1 && !s31_locked);
        se.ei_addr = 1;
        se.ei_state = FLASH_ERASE_DONE;
        assert(esp32s31_flash_erase(&s31, &se) == EINVAL);
        assert(se.ei_state == FLASH_ERASE_FAILED);
        se.ei_addr = -1;
        se.ei_len = 4096;
        assert(esp32s31_flash_erase(&s31, &se) == EINVAL);
        assert(s31_calls == 2);
        se.ei_addr = 0;
        se.ei_len = -1;
        assert(esp32s31_flash_erase(&s31, &se) == EINVAL);
        assert(s31_calls == 2);

        assert(esp32s31_flash_read(&s31, -1, 1, &retlen, buffer) == EINVAL);
        assert(retlen == 0 && s31_calls == 2 && s31_dma_syncs == 0);
        retlen = 99;
        assert(esp32s31_flash_write(&s31, -1, 4, &retlen, buffer) == EINVAL);
        assert(retlen == 0 && s31_calls == 2 && s31_dma_syncs == 0);
        retlen = 99;
        assert(esp32s31_flash_read(&s31, 0, SIZE_MAX, &retlen, buffer) == EINVAL);
        assert(retlen == 0 && s31_calls == 2 && s31_dma_syncs == 0);
    }
    return 0;
}
"""

out_dir = pathlib.Path(args.out_dir).resolve()
out_dir.mkdir(parents=True, exist_ok=True)
source = out_dir / "t_flash_erase.c"
binary = out_dir / "t_flash_erase"
source.write_text(fixture)
subprocess.run([args.cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                str(source), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
