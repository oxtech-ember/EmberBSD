#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the real flash_sync() body with mocked device and interface state."""

import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
args = parser.parse_args()

source = pathlib.Path(__file__).resolve().parents[1] / "flash.c"
text = source.read_text()
match = re.search(r"int\nflash_sync\(device_t self\)\n\{", text)
assert match, "missing flash_sync()"
depth = 1
end = match.end()
while depth:
    if text[end] == "{":
        depth += 1
    elif text[end] == "}":
        depth -= 1
    end += 1
function = text[match.start():end]

out_dir = pathlib.Path(args.out_dir).resolve()
out_dir.mkdir(parents=True, exist_ok=True)
fixture = out_dir / "t_flash_sync.c"
binary = out_dir / "t_flash_sync"
fixture.write_text(r"""
#include <assert.h>
#include <errno.h>
#include <stddef.h>
typedef struct device *device_t;
struct flash_interface { int (*sync)(device_t); };
struct flash_softc {
    int sc_readonly;
    struct flash_interface *flash_if;
};
struct device { struct flash_softc *sc; device_t parent; };
#define device_private(d) ((d)->sc)
#define device_parent(d) ((d)->parent)

static int calls;
static device_t seen_device;
static int callback_result;
static int sync_callback(device_t dev)
{
    calls++;
    seen_device = dev;
    return callback_result;
}

""" + function + r"""

int main(void)
{
    struct device parent = {0};
    struct flash_interface flash_if = { .sync = sync_callback };
    struct flash_softc sc = { .flash_if = &flash_if };
    struct device self = { .sc = &sc, .parent = &parent };

    sc.sc_readonly = 1;
    assert(flash_sync(&self) == 0);
    assert(calls == 0);

    sc.sc_readonly = 0;
    flash_if.sync = NULL;
    assert(flash_sync(&self) == 0);
    assert(calls == 0);

    flash_if.sync = sync_callback;
    callback_result = 0;
    assert(flash_sync(&self) == 0);
    assert(calls == 1 && seen_device == &parent);

    callback_result = EIO;
    assert(flash_sync(&self) == EIO);
    assert(calls == 2 && seen_device == &parent);
    return 0;
}
""")
subprocess.run([args.cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                str(fixture), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
