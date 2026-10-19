/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted production UART ring/IRQ progress contract. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define ISSET(v, b) ((v) & (b))
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL %d: %s\n", \
    __LINE__, #e); abort(); } } while (0)
#define ZYNQUART_ISALIVE(sc) ((sc)->enabled)
#define ZYNQUART_RX_TTY_OVERFLOWED __BIT(1)
#define ZYNQUART_RX_IBUF_BLOCKED __BIT(2)
#define ZYNQUART_RX_IBUF_OVERFLOWED __BIT(3)
#define cn_check_magic(dev, value, state) ((void)(value))
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
struct zynquart_softc {
    struct { bus_space_tag_t ur_iot; bus_space_handle_t ur_ioh; } sc_regs;
    unsigned sc_rbuf_in, sc_rbuf_out, sc_rbuf_size, sc_r_hiwat, sc_rx_flags;
    uint16_t *sc_rbuf;
    bool enabled, sc_rx_ready, sc_lock;
    void *sc_si;
};
#include "zynq_uartreg.h"
static unsigned fifo_left, fifo_reads, status_reads, softints, flow_changes;
static bool rx_irq;
static uint32_t
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, unsigned reg)
{
    if (reg == UART_CHNL_INT_STS)
        return INT_RTRIG;
    if (reg == UART_CHANNEL_STS) {
        CHECK(++status_reads < 100); /* a full software ring must not spin. */
        return (fifo_left == 0 ? STS_REMPTY : 0) | STS_TEMPTY;
    }
    CHECK(reg == UART_TX_RX_FIFO && fifo_left != 0);
    fifo_left--;
    return ++fifo_reads;
}
static void
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, unsigned reg, uint32_t v)
{ CHECK(reg == UART_CHNL_INT_STS); }
static void mutex_spin_enter(bool *p) { CHECK(!*p); *p = true; }
static void mutex_spin_exit(bool *p) { CHECK(*p); *p = false; }
static void softint_schedule(void *si) { softints++; }
static void zynquart_hwiflow(struct zynquart_softc *sc) { flow_changes++; }
static void zynquart_control_rxint(struct zynquart_softc *sc, bool enabled)
{ rx_irq = enabled; }
static void zynquartintr_send(struct zynquart_softc *sc) { }
#include "uart-bodies.h"

static void
test_capacity(void)
{
    struct zynquart_softc sc = {.sc_rbuf_size = 8};
    for (unsigned out = 0; out < 8; out++) {
        for (unsigned used = 0; used < 8; used++) {
            sc.sc_rbuf_out = out;
            sc.sc_rbuf_in = (out + used) % 8;
            CHECK(ZYNQUART_RBUF_AVAIL((&sc)) == used);
            CHECK(ZYNQUART_RBUF_SPACE((&sc)) == 7 - used);
        }
    }
}
static void
test_irq(unsigned used, unsigned queued)
{
    uint16_t ring[8];
    struct zynquart_softc sc = {.enabled = true, .sc_rbuf_size = 8,
        .sc_rbuf_in = (6 + used) % 8, .sc_rbuf_out = 6,
        .sc_rbuf = ring, .sc_r_hiwat = 2};
    const unsigned accepted = queued < 7 - used ? queued : 7 - used;
    memset(ring, 0xa5, sizeof(ring));
    fifo_left = queued;
    fifo_reads = status_reads = softints = flow_changes = 0;
    rx_irq = true;
    CHECK(zynquartintr(&sc) == 1);
    CHECK(!sc.sc_lock && softints == 1);
    CHECK(fifo_reads == accepted && fifo_left == queued - accepted);
    CHECK(ZYNQUART_RBUF_AVAIL((&sc)) == used + accepted);
    CHECK(rx_irq == (used + accepted < 7));
    for (unsigned i = 0; i < used; i++)
        CHECK(ring[(6 + i) % 8] == 0xa5a5); /* unread bytes cannot be overwritten. */
    for (unsigned i = 0; i < accepted; i++)
        CHECK(ring[(6 + used + i) % 8] == i + 1);
}
int
main(void)
{
    test_capacity();
    test_irq(0, 3);
    test_irq(0, 20);
    test_irq(5, 20);
    test_irq(7, 20);
    puts("PASS UART ring capacity, wrap, retained bytes and bounded full-ring IRQ");
    return 0;
}
