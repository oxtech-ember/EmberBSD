#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the real generic SPI transfer validation for 3-wire phases."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', required=True, type=Path)
p.add_argument('--host-prefix', required=True, type=Path)
p.add_argument('--work', required=True, type=Path)
a = p.parse_args()
host = a.host_prefix.resolve(strict=True)
cc = (host / 'bin/clang').resolve(strict=True)
ld = (host / 'bin' / ('ld64.lld' if sys.platform == 'darwin' else 'ld.lld')).resolve(strict=True)
if not cc.is_relative_to(host) or not ld.is_relative_to(host):
    p.error('compiler and linker must resolve within host prefix')
env = dict(os.environ, PATH=os.pathsep.join((str(host / 'bin'), str(host / 'gnu/bin'))))
src = (a.source / 'sys/dev/spi/spi.c').read_text()
start = src.index('\nint\nspi_transfer(struct spi_handle *sh, struct spi_transfer *st)')
end = src.index('\n}', start) + 2
transfer = src[start:end]

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int u_int;
#define SPI_CAP_DUAL_TX 0x01
#define SPI_CAP_DUAL_RX 0x02
#define SPI_CAP_QUAD_TX 0x04
#define SPI_CAP_QUAD_RX 0x08
#define SPI_CAP_3WIRE   0x10
#define SPI_F_DONE      0x01
#define SPI_F_ERROR     0x02
#define SPI_F_3WIRE     0x04
struct spi_chunk {
    struct spi_chunk *chunk_next;
    int chunk_count, chunk_wresid, chunk_rresid;
    const uint8_t *chunk_write, *chunk_wptr;
    uint8_t *chunk_read, *chunk_rptr;
    unsigned int chunk_bus_width;
};
struct spi_transfer {
    struct spi_chunk *st_chunks;
    volatile int st_flags;
    int st_errno, st_slave;
    void *st_spiprivate;
};
struct spi_controller {
    void *sct_cookie;
    int sct_nslaves;
    int (*sct_configure)(void *, int, int, int);
    int (*sct_transfer)(void *, struct spi_transfer *);
    u_int sct_caps;
};
struct spi_softc {
    struct spi_controller sc_controller;
    int sc_slave, sc_mode, sc_speed;
    int configure_calls, transfer_calls, acquire_calls, done_calls;
};
struct spi_handle {
    struct spi_softc *sh_sc;
    struct spi_controller *sh_controller;
    int sh_slave, sh_mode, sh_speed;
};
static void spi_acquire(struct spi_handle *sh) { sh->sh_sc->acquire_calls++; }
static void spi_done(struct spi_transfer *st, int err) {
    if (err != 0) { st->st_flags |= SPI_F_ERROR; st->st_errno = err; }
    st->st_flags |= SPI_F_DONE;
}
static int configure(void *cookie, int slave, int mode, int speed) {
    struct spi_softc *sc = cookie; (void)slave; (void)mode; (void)speed;
    sc->configure_calls++; return 0;
}
static int transfer(void *cookie, struct spi_transfer *st) {
    struct spi_softc *sc = cookie; (void)st; sc->transfer_calls++; return 0;
}
'''

suffix = r'''
static void reset(struct spi_softc *sc, struct spi_handle *sh, u_int caps) {
    memset(sc, 0, sizeof(*sc));
    sc->sc_controller.sct_cookie = sc;
    sc->sc_controller.sct_nslaves = 1;
    sc->sc_controller.sct_configure = configure;
    sc->sc_controller.sct_transfer = transfer;
    sc->sc_controller.sct_caps = caps;
    sc->sc_slave = -1; sc->sc_mode = -1; sc->sc_speed = 0;
    sh->sh_sc = sc; sh->sh_controller = &sc->sc_controller;
    sh->sh_slave = 0; sh->sh_mode = 0; sh->sh_speed = 1000000;
}
int main(void) {
    struct spi_softc sc; struct spi_handle sh;
    uint8_t tx[4] = {3,0,4,0}, rx[3] = {0};
    struct spi_chunk send = { .chunk_count=sizeof(tx), .chunk_write=tx,
        .chunk_bus_width=1 };
    struct spi_chunk recv = { .chunk_count=sizeof(rx), .chunk_read=rx,
        .chunk_bus_width=1 };
    struct spi_transfer st = { .st_chunks=&send, .st_flags=SPI_F_3WIRE };

    reset(&sc, &sh, SPI_CAP_3WIRE);
    send.chunk_next = &recv;
    assert(spi_transfer(&sh, &st) == 0);
    assert(sc.configure_calls == 1 && sc.transfer_calls == 1);

    reset(&sc, &sh, SPI_CAP_3WIRE);
    struct spi_chunk zero_width = { .chunk_count=1, .chunk_write=tx,
        .chunk_bus_width=0 };
    st = (struct spi_transfer){ .st_chunks=&zero_width,
        .st_flags=SPI_F_3WIRE };
    assert(spi_transfer(&sh, &st) == 0);
    assert(!(st.st_flags & SPI_F_ERROR));
    assert(sc.configure_calls == 1 && sc.transfer_calls == 1);

    reset(&sc, &sh, 0);
    st = (struct spi_transfer){ .st_chunks=&send, .st_flags=SPI_F_3WIRE };
    assert(spi_transfer(&sh, &st) == 0);
    assert((st.st_flags & SPI_F_ERROR) && st.st_errno == EOPNOTSUPP);
    assert(sc.configure_calls == 0 && sc.transfer_calls == 0);

    reset(&sc, &sh, SPI_CAP_3WIRE);
    struct spi_chunk full = { .chunk_count=1, .chunk_write=tx,
        .chunk_read=rx, .chunk_bus_width=1 };
    st = (struct spi_transfer){ .st_chunks=&full, .st_flags=SPI_F_3WIRE };
    assert(spi_transfer(&sh, &st) == 0);
    assert((st.st_flags & SPI_F_ERROR) && st.st_errno == EINVAL);
    assert(sc.configure_calls == 0 && sc.transfer_calls == 0);

    reset(&sc, &sh, SPI_CAP_3WIRE);
    struct spi_chunk multiline = { .chunk_count=1, .chunk_write=tx,
        .chunk_bus_width=2 };
    st = (struct spi_transfer){ .st_chunks=&multiline, .st_flags=SPI_F_3WIRE };
    assert(spi_transfer(&sh, &st) == 0);
    assert((st.st_flags & SPI_F_ERROR) && st.st_errno == EINVAL);
    assert(sc.configure_calls == 0 && sc.transfer_calls == 0);

    reset(&sc, &sh, 0);
    st = (struct spi_transfer){ .st_chunks=&send, .st_flags=0 };
    assert(spi_transfer(&sh, &st) == 0);
    assert(sc.configure_calls == 1 && sc.transfer_calls == 1);
    puts("PASS generic SPI 3-wire capability and phase validation");
    return 0;
}
'''

a.work.mkdir(parents=True, exist_ok=True)
cfile = a.work / 'spi-3wire-test.c'
exe = a.work / 'spi-3wire-test'
cfile.write_text(prefix + transfer + suffix)
subprocess.run([str(cc), '-fuse-ld=lld', '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all', str(cfile), '-o', str(exe)],
               env=env, check=True)
subprocess.run([str(exe)], env=env, check=True)
