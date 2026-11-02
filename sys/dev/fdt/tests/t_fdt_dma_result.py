#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project.
"""Exercise the production FDT DMA result dispatch with old and new providers."""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
args = parser.parse_args()
source = pathlib.Path(__file__).resolve().parents[1] / "fdt_dma.c"
text = source.read_text(encoding="utf-8")
start = text.index("int\nfdtbus_dma_result(")
end = text.index("\nvoid\nfdtbus_dma_halt(", start)
function = text[start:end]
fixture = r'''#include <stddef.h>
#include <errno.h>
#include <assert.h>
typedef void *device_t;
struct fdtbus_dma_result { int dres_error; size_t dres_bytes; };
struct fdtbus_dma_controller_func {
    int (*result)(device_t, void *, struct fdtbus_dma_result *);
    int (*halt_checked)(device_t, void *);
};
struct fdtbus_dma_controller {
    device_t dma_dev;
    const struct fdtbus_dma_controller_func *dma_funcs;
};
struct fdtbus_dma {
    struct fdtbus_dma_controller *dma_dc;
    void *dma_priv;
};
''' + function + r'''
static int done(device_t dev, void *priv, struct fdtbus_dma_result *result) {
    assert(dev == (void *)1 && priv == (void *)2);
    result->dres_error = EIO;
    result->dres_bytes = 123;
    return 0;
}
static int stopped(device_t dev, void *priv) {
    assert(dev == (void *)1 && priv == (void *)2);
    return EBUSY;
}
int main(void) {
    const struct fdtbus_dma_controller_func old = { .result = NULL };
    const struct fdtbus_dma_controller_func new = {
        .result = done, .halt_checked = stopped
    };
    struct fdtbus_dma_controller dc = { (void *)1, &old };
    struct fdtbus_dma dma = { &dc, (void *)2 };
    struct fdtbus_dma_result result = { 0, 0 };
    assert(fdtbus_dma_result(&dma, &result) == ENOTSUP);
    assert(fdtbus_dma_halt_checked(&dma) == ENOTSUP);
    dc.dma_funcs = &new;
    assert(fdtbus_dma_result(&dma, &result) == 0);
    assert(result.dres_error == EIO && result.dres_bytes == 123);
    assert(fdtbus_dma_halt_checked(&dma) == EBUSY);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="fdt-dma-result-") as name:
    work = pathlib.Path(name)
    (work / "test.c").write_text(fixture, encoding="utf-8")
    subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", str(work / "test.c"),
                    "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
