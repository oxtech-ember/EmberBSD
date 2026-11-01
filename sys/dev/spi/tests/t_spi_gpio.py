#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Run the actual GPIO SPI configure and transfer functions on fake pins."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', required=True, type=Path)
parser.add_argument('--host-prefix', required=True, type=Path)
parser.add_argument('--work', required=True, type=Path)
args = parser.parse_args()
host = args.host_prefix.resolve(strict=True)
cc = (host / 'bin/clang').resolve(strict=True)
ld = (host / 'bin' / ('ld64.lld' if sys.platform == 'darwin' else 'ld.lld')).resolve(strict=True)
if not cc.is_relative_to(host) or not ld.is_relative_to(host):
    parser.error('compiler and linker must resolve within host prefix')
env = dict(os.environ, PATH=os.pathsep.join((str(host / 'bin'), str(host / 'gnu/bin'))))
source = (args.source / 'sys/dev/spi/spi_gpio.c').read_text()


def extract(name):
    pos = source.index('\n' + name + '(')
    start = source.rfind('\nstatic ', 0, pos)
    end = source.index('\n}', pos) + 2
    return source[start + 1:end] + '\n'


production = ''.join(extract(name) for name in (
    'spigpio_configure', 'spigpio_validate', 'spigpio_shift_byte', 'spigpio_transfer'))
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int u_int;
#define SPI_MODE_CPHA 1
#define SPI_MODE_CPOL 2
#define SPI_MODE_0 0
#define SPI_MODE_1 SPI_MODE_CPHA
#define SPI_MODE_2 SPI_MODE_CPOL
#define SPI_MODE_3 (SPI_MODE_CPHA | SPI_MODE_CPOL)
#define SPI_F_DONE 1
#define SPI_F_ERROR 2
#define SPI_F_3WIRE 4
#define SPIGPIO_MAX_FREQUENCY 100000
struct fdtbus_gpio_pin { int id, value, active_low; };
struct spi_chunk {
    struct spi_chunk *chunk_next;
    int chunk_count, chunk_wresid, chunk_rresid;
    const uint8_t *chunk_write, *chunk_wptr;
    uint8_t *chunk_read, *chunk_rptr;
    unsigned int chunk_bus_width;
};
struct spi_transfer { struct spi_chunk *st_chunks; volatile int st_flags; int st_errno; };
struct spigpio_softc {
    struct fdtbus_gpio_pin *sc_sck, *sc_mosi, *sc_miso, *sc_cs;
    unsigned int sc_mode, sc_speed, sc_half_period;
    bool sc_configured;
};
struct event { int pin, value; };
static struct event events[1024];
static unsigned int event_count, delay_count, read_count, trailing_count;
static int read_bits[32];
static unsigned int read_len;
static unsigned int expected_mode;
static const uint8_t *expected_tx;
static unsigned int expected_tx_len, leading_count;
static unsigned int cpol, cpha;
static int cpha1_mosi_pending;
static int clock_started;
static unsigned int last_sck_delay_count;
static unsigned int leading_events[32], trailing_events[32], mosi_events[32];
static unsigned int mosi_event_count;
static void reset_events(void) {
    event_count = delay_count = read_count = leading_count = 0;
    trailing_count = mosi_event_count = 0;
    cpha1_mosi_pending = 0;
    clock_started = 0;
    last_sck_delay_count = 0;
    memset(events, 0, sizeof(events));
}
static void fdtbus_gpio_write(struct fdtbus_gpio_pin *pin, int value) {
    assert(event_count < sizeof(events) / sizeof(events[0]));
    pin->value = !!value ^ pin->active_low;
    events[event_count++] = (struct event){ pin->id, pin->value };
    if (pin->id == 4 && value == 1)
        clock_started = 1;
    if (pin->id == 1 && clock_started && !!value == !cpol) {
        unsigned int bit = leading_count++;
        assert(bit < expected_tx_len * 8);
        leading_events[bit] = event_count - 1;
        if (cpha)
            cpha1_mosi_pending = 1;
        else {
            assert(events[event_count - 2].pin == 2);
            assert(events[event_count - 2].value ==
                ((expected_tx[bit / 8] >> (7 - bit % 8)) & 1));
        }
    } else if (pin->id == 1 && clock_started) {
        assert(trailing_count < sizeof(trailing_events) /
            sizeof(trailing_events[0]));
        trailing_events[trailing_count++] = event_count - 1;
    }
    if (pin->id == 2 && clock_started) {
        assert(mosi_event_count < sizeof(mosi_events) /
            sizeof(mosi_events[0]));
        mosi_events[mosi_event_count++] = event_count - 1;
        if (cpha && cpha1_mosi_pending) {
            unsigned int bit = leading_count - 1;
            assert(pin->value ==
                ((expected_tx[bit / 8] >> (7 - bit % 8)) & 1));
            cpha1_mosi_pending = 0;
        }
    }
    if (pin->id == 1)
        last_sck_delay_count = delay_count;
    if (pin->id == 4 && value == 0)
        assert(delay_count > last_sck_delay_count);
}
static int fdtbus_gpio_read(struct fdtbus_gpio_pin *pin) {
    assert(pin->id == 3 && read_count < read_len);
    assert(events[event_count - 1].pin == 1);
    assert(events[event_count - 1].value == (cpha ? (int)cpol : (int)!cpol));
    return read_bits[read_count++];
}
static void delay(unsigned int us) { assert(us == 5); delay_count++; }
static void spi_done(struct spi_transfer *st, int error) {
    st->st_errno = error;
    st->st_flags |= error ? SPI_F_ERROR : SPI_F_DONE;
}
'''
suffix = r'''
static void reset_case(struct spigpio_softc *sc, struct fdtbus_gpio_pin *pins,
    unsigned int mode, int cs_active_low) {
    memset(sc, 0, sizeof(*sc));
    pins[0] = (struct fdtbus_gpio_pin){ .id=1 };
    pins[1] = (struct fdtbus_gpio_pin){ .id=2 };
    pins[2] = (struct fdtbus_gpio_pin){ .id=3 };
    pins[3] = (struct fdtbus_gpio_pin){ .id=4, .value=cs_active_low,
        .active_low=cs_active_low };
    sc->sc_sck=&pins[0]; sc->sc_mosi=&pins[1]; sc->sc_miso=&pins[2];
    sc->sc_cs=&pins[3];
    assert(pins[3].value==cs_active_low);
    assert(spigpio_configure(sc,0,(int)mode,100000)==0);
    assert(sc->sc_half_period==5 && sc->sc_speed==100000);
    cpol = !!(mode & SPI_MODE_CPOL); cpha = !!(mode & SPI_MODE_CPHA);
    expected_mode = mode; (void)expected_mode;
}
static void check_transaction(unsigned int mode, int cs_active_low) {
    struct spigpio_softc sc; struct fdtbus_gpio_pin pins[4];
    const uint8_t tx0[]={0x96}, tx1[]={0x81};
    uint8_t rx0[1]={0}, rx1[1]={0};
    struct spi_chunk c0={ .chunk_count=1,.chunk_wresid=1,.chunk_rresid=1,
        .chunk_write=tx0,.chunk_wptr=tx0,.chunk_read=rx0,.chunk_rptr=rx0,
        .chunk_bus_width=1 };
    struct spi_chunk c1={ .chunk_count=1,.chunk_wresid=1,.chunk_rresid=1,
        .chunk_write=tx1,.chunk_wptr=tx1,.chunk_read=rx1,.chunk_rptr=rx1,
        .chunk_bus_width=1 };
    struct spi_transfer st={ .st_chunks=&c0 };
    reset_case(&sc,pins,mode,cs_active_low); reset_events(); c0.chunk_next=&c1;
    expected_tx=(const uint8_t[]){0x96,0x81}; expected_tx_len=2;
    read_len=16;
    for (unsigned int i=0;i<read_len;i++) read_bits[i]=(i % 3)==0;
    assert(spigpio_transfer(&sc,&st)==0);
    assert((st.st_flags & SPI_F_DONE) && !(st.st_flags & SPI_F_ERROR));
    assert(c0.chunk_wresid==0 && c1.chunk_wresid==0);
    assert(leading_count==16 && read_count==16 && delay_count==34);
    assert(cpha1_mosi_pending==0);
    assert(trailing_count==16 && mosi_event_count==16);
    for (unsigned int bit=0;bit<16;bit++) {
        if (cpha) {
            assert(leading_events[bit] < mosi_events[bit]);
            assert(mosi_events[bit] < trailing_events[bit]);
        } else {
            assert(mosi_events[bit] < leading_events[bit]);
            assert(leading_events[bit] < trailing_events[bit]);
        }
        if (bit+1<16) {
            assert(trailing_events[bit] < leading_events[bit+1]);
            if (cpha) {
                for (unsigned int i=trailing_events[bit]+1;
                    i<leading_events[bit+1];i++)
                    assert(events[i].pin!=2);
            }
        }
    }
    unsigned int cs_count=0;
    for (unsigned int i=0;i<event_count;i++) if (events[i].pin==4) {
        assert((cs_count==0 && events[i].value==!cs_active_low) ||
               (cs_count==1 && events[i].value==cs_active_low));
        if (cs_count==0) assert(i > 0 && events[i-1].pin==1 &&
            events[i-1].value==(int)cpol);
        if (cs_count==1) assert(i+1==event_count);
        cs_count++;
    }
    assert(cs_count==2 && pins[3].value==cs_active_low);
    uint8_t expected0=0, expected1=0;
    for (unsigned int i=0;i<8;i++) {
        if ((i % 3)==0) expected0 |= 0x80 >> i;
        if (((i+8) % 3)==0) expected1 |= 0x80 >> i;
    }
    assert(rx0[0]==expected0 && rx1[0]==expected1);
}
int main(void) {
    for (unsigned int mode=0;mode<=3;mode++) {
        check_transaction(mode,0);
        check_transaction(mode,1);
    }
    struct spigpio_softc sc; struct fdtbus_gpio_pin pins[4];
    const uint8_t tx[]={0xaa}; uint8_t rx[1];
    struct spi_chunk valid={ .chunk_count=1,.chunk_wresid=1,.chunk_rresid=1,
        .chunk_write=tx,.chunk_wptr=tx,.chunk_bus_width=1 };
    struct spi_chunk invalid={ .chunk_count=1,.chunk_wresid=1,.chunk_rresid=1,
        .chunk_read=rx,.chunk_rptr=rx,.chunk_bus_width=1 };
    struct spi_transfer st={ .st_chunks=&valid };
    reset_case(&sc,pins,SPI_MODE_0,1); sc.sc_miso=NULL;
    expected_tx=tx; expected_tx_len=1;
    reset_events(); assert(spigpio_transfer(&sc,&st)==0);
    assert((st.st_flags & SPI_F_DONE) && pins[3].value==1);
    reset_events(); st=(struct spi_transfer){.st_chunks=&valid,.st_flags=SPI_F_3WIRE};
    assert(spigpio_transfer(&sc,&st)==0);
    assert((st.st_flags & SPI_F_ERROR) && st.st_errno==EOPNOTSUPP);
    assert(event_count==0 && pins[3].value==1);
    reset_events(); st=(struct spi_transfer){.st_chunks=&valid}; valid.chunk_next=&invalid;
    assert(spigpio_transfer(&sc,&st)==0);
    assert((st.st_flags & SPI_F_ERROR) && st.st_errno==EOPNOTSUPP);
    assert(event_count==0 && pins[3].value==1);
    valid.chunk_next=NULL; valid.chunk_bus_width=4;
    reset_events(); st=(struct spi_transfer){.st_chunks=&valid};
    assert(spigpio_transfer(&sc,&st)==0);
    assert((st.st_flags & SPI_F_ERROR) && st.st_errno==EOPNOTSUPP);
    assert(event_count==0 && pins[3].value==1);
    assert(spigpio_configure(&sc,1,SPI_MODE_0,100000)==EINVAL);
    assert(spigpio_configure(&sc,0,4,100000)==EINVAL);
    assert(spigpio_configure(&sc,0,SPI_MODE_0,100001)==EINVAL);
    assert(spigpio_configure(&sc,0,SPI_MODE_0,0)==EINVAL);
    puts("PASS: GPIO SPI modes, bit order, active-low/high CS, multi-chunk hold and rejections");
    return 0;
}
'''
args.work.mkdir(parents=True, exist_ok=False)
cfile = args.work / 't_spi_gpio.c'
binary = args.work / 't_spi_gpio'
cfile.write_text(prefix + production + suffix)
subprocess.run([str(cc), '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(cfile),
    '-o', str(binary)], check=True, env=env)
subprocess.run([str(binary)], check=True, env=env)
