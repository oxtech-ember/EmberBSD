#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the production flash I/O flush barrier and S31 sync ordering."""

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
    if not match:
        raise SystemExit(f"missing production function in {path}: {pattern}")
    depth = 1
    end = match.end()
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[match.start():end]


io_source = root / "sys/dev/flash/flash_io.c"
cache_sync = extract(io_source, r"int\nflash_io_cache_sync\(struct flash_io \*fio\)\n\{")
barrier = extract(io_source, r"int\nflash_io_sync\(struct flash_io \*fio\)\n\{")
done = extract(io_source, r"void\nflash_io_done\(struct flash_io \*fio, struct buf \*bp, int error\)\n\{")
s31_sync = extract(root / "sys/arch/riscv/espressif/esp32s31_flash.c",
                   r"static int\nesp32s31_flash_sync\(device_t self\)\n\{")
nor_sync = extract(root / "sys/dev/nor/nor.c",
                   r"static int\nnor_flash_sync\(device_t self\)\n\{")
nand_sync = extract(root / "sys/dev/nand/nand.c",
                    r"static int\nnand_flash_sync\(device_t self\)\n\{")
for driver, callback in (("nor", "nor_flash_sync"), ("nand", "nand_flash_sync")):
    source = (root / f"sys/dev/{driver}/{driver}.c").read_text()
    initializer = source.split(f"struct flash_interface {driver}_flash_if = {{", 1)[1].split("};", 1)[0]
    if f".sync = {callback}" not in initializer:
        raise SystemExit(f"{driver} flash interface does not dispatch sync to {callback}")

fixture = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct device *device_t;
typedef int64_t flash_off_t;
typedef uint64_t flash_addr_t;
typedef int64_t daddr_t;
typedef struct { int locked; } kmutex_t;
struct buf { int b_error; size_t b_resid; int completed; };
struct bufq_state { struct buf *bp; };
struct flash_erase_instruction {
    flash_off_t ei_addr, ei_len;
    void (*ei_callback)(struct flash_erase_instruction *);
    unsigned long ei_priv;
    unsigned char ei_state;
};
struct flash_interface {
    int (*erase)(device_t, struct flash_erase_instruction *);
    int (*write)(device_t, flash_off_t, size_t, size_t *, const uint8_t *);
    uint32_t erasesize;
};
struct flash_io {
    device_t fio_dev;
    struct bufq_state *fio_bufq;
    uint8_t *fio_data;
    daddr_t fio_block;
    kmutex_t fio_lock;
    bool fio_write_pending;
    struct flash_interface *fio_if;
};
struct device { void *private; };
struct esp32s31_flash_softc {
    struct flash_io sc_io;
    kmutex_t sc_lock;
    int sc_eid;
};
struct nor_softc { struct flash_io sc_flash_io; };
struct nand_softc { struct flash_io sc_flash_io; };
struct sbiret { long error, value; };

static int event_log[8], event_count;
static int erase_result, write_result, short_write, rom_result;
static int erase_calls, write_calls, rom_calls;
static int *sc_lock_ptr;
#define KASSERT(c) assert(c)
#define FLDPRINTF(x) ((void)0)
#define PRIu32 "u"
#define device_private(d) ((d)->private)
#define mutex_enter(m) do { assert(!(m)->locked); (m)->locked = 1; } while (0)
#define mutex_exit(m) do { assert((m)->locked); (m)->locked = 0; } while (0)
#define mutex_owned(m) ((m)->locked)
#define SBI_CALL0(eid, func) mock_sbi_call0((eid), (func))
#define S31_SBI_FLASH_SYNC 10

static struct buf *bufq_get(struct bufq_state *q)
{
    struct buf *bp = q->bp;
    q->bp = NULL;
    return bp;
}
static void biodone(struct buf *bp) { bp->completed++; }
static void aprint_error_dev(device_t d, const char *fmt, ...)
{ (void)d; (void)fmt; }
static int mock_erase(device_t d, struct flash_erase_instruction *ei)
{
    (void)d; (void)ei;
    assert(!sc_lock_ptr || !*sc_lock_ptr);
    erase_calls++;
    event_log[event_count++] = 1;
    return erase_result;
}
static int mock_write(device_t d, flash_off_t addr, size_t len,
    size_t *retlen, const uint8_t *data)
{
    (void)d; (void)addr; (void)data;
    assert(!sc_lock_ptr || !*sc_lock_ptr);
    write_calls++;
    event_log[event_count++] = 2;
    *retlen = short_write ? len - 1 : len;
    return write_result;
}
static struct sbiret mock_sbi_call0(int eid, int func)
{
    (void)eid; (void)func;
    assert(sc_lock_ptr != NULL && *sc_lock_ptr);
    rom_calls++;
    event_log[event_count++] = 3;
    return (struct sbiret){ .error = rom_result, .value = 0 };
}
static int esp32s31_flash_error(long error) { return (int)error; }

""" + done + cache_sync + barrier + s31_sync + nor_sync + nand_sync + r"""
static void reset_events(void)
{
    event_count = erase_calls = write_calls = rom_calls = 0;
    erase_result = write_result = rom_result = 0;
    short_write = 0;
}

int main(void)
{
    struct device parent = {0};
    uint8_t data[4096] = {0};
    struct buf bp = { .b_resid = sizeof(data) };
    struct bufq_state queue = { .bp = &bp };
    struct flash_interface fi = { .erase = mock_erase, .write = mock_write,
        .erasesize = sizeof(data) };
    struct flash_io fio = { .fio_dev = &parent, .fio_bufq = &queue,
        .fio_data = data, .fio_block = 2, .fio_write_pending = true,
        .fio_if = &fi };

    assert(flash_io_sync(&fio) == 0);
    assert(erase_calls == 1 && write_calls == 1);
    assert(!fio.fio_write_pending && queue.bp == NULL);
    assert(bp.completed == 1 && bp.b_error == 0 && bp.b_resid == 0);

    reset_events();
    bp = (struct buf){ .b_resid = sizeof(data) };
    queue.bp = &bp;
    fio.fio_write_pending = true;
    erase_result = EIO;
    assert(flash_io_sync(&fio) == EIO);
    assert(erase_calls == 1 && write_calls == 0);
    assert(bp.completed == 1 && bp.b_error == EIO && !fio.fio_write_pending);

    reset_events();
    bp = (struct buf){ .b_resid = sizeof(data) };
    queue.bp = &bp;
    fio.fio_write_pending = true;
    short_write = 1;
    assert(flash_io_sync(&fio) == EIO);
    assert(erase_calls == 1 && write_calls == 1);
    assert(bp.completed == 1 && bp.b_error == EIO);

    reset_events();
    fio.fio_write_pending = false;
    queue.bp = NULL;
    assert(flash_io_sync(&fio) == 0);
    assert(erase_calls == 0 && write_calls == 0 && event_count == 0);

    {
        struct esp32s31_flash_softc sc = {0};
        struct device s31 = { .private = &sc };
        bp = (struct buf){ .b_resid = sizeof(data) };
        queue.bp = &bp;
        fio.fio_write_pending = true;
        sc.sc_io = fio;
        sc.sc_io.fio_bufq = &queue;
        sc.sc_io.fio_write_pending = true;
        sc.sc_io.fio_if = &fi;
        sc.sc_io.fio_data = data;
        sc_lock_ptr = &sc.sc_lock.locked;
        reset_events();
        assert(esp32s31_flash_sync(&s31) == 0);
        assert(event_count == 3 && event_log[0] == 1 &&
            event_log[1] == 2 && event_log[2] == 3);
        assert(rom_calls == 1 && bp.completed == 1 && bp.b_error == 0);
        sc_lock_ptr = NULL;
    }
    {
        struct nor_softc nor_sc = { .sc_flash_io = fio };
        struct nand_softc nand_sc = { .sc_flash_io = fio };
        struct device nor = { .private = &nor_sc };
        struct device nand = { .private = &nand_sc };
        bp = (struct buf){ .b_resid = sizeof(data) };
        queue.bp = &bp;
        nor_sc.sc_flash_io.fio_bufq = &queue;
        nor_sc.sc_flash_io.fio_write_pending = true;
        reset_events();
        assert(nor_flash_sync(&nor) == 0);
        assert(erase_calls == 1 && write_calls == 1 && bp.completed == 1);

        bp = (struct buf){ .b_resid = sizeof(data) };
        queue.bp = &bp;
        nand_sc.sc_flash_io.fio_bufq = &queue;
        nand_sc.sc_flash_io.fio_write_pending = true;
        erase_result = EIO;
        assert(nand_flash_sync(&nand) == EIO);
        assert(bp.completed == 1 && bp.b_error == EIO);
    }
    return 0;
}
"""

out_dir = pathlib.Path(args.out_dir).resolve()
out_dir.mkdir(parents=True, exist_ok=True)
fixture_path = out_dir / "t_flash_io_sync.c"
binary = out_dir / "t_flash_io_sync"
fixture_path.write_text(fixture)
subprocess.run([args.cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter",
                str(fixture_path), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
