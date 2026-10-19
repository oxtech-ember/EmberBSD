/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD actual-source Zynq SPI attachment lifecycle regression. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

typedef unsigned int u_int;
typedef uintptr_t bus_addr_t;
typedef uintptr_t bus_space_handle_t;
typedef size_t bus_size_t;
struct fixture;
typedef struct fixture *bus_space_tag_t;
struct zynq_spi_softc;
struct device { struct zynq_spi_softc *private; };
typedef struct device *device_t;
struct spi_transfer;
struct spi_controller {
    void *sct_cookie;
    int sct_nslaves;
    int (*sct_configure)(void *, int, int, int);
    int (*sct_transfer)(void *, struct spi_transfer *);
};
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
struct clk { unsigned int index; bool enabled; };
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
/* ACTUAL_REGISTERS */
/* ACTUAL_SOFTC */

enum failure {
    NONE, NO_PCLK, NO_REFCLK, ENABLE_PCLK, ENABLE_REFCLK,
    ZERO_RATE, NO_REG, SHORT_REG, MAP, DRAIN
};
struct fixture {
    enum failure fail;
    struct clk clocks[2];
    unsigned int enables[2], disables[2];
    unsigned int maps, unmaps, registered, attached, errors, rx_reads;
    bool mapped;
    uint32_t regs[0x30 / 4];
    char trace[32];
    size_t trace_length;
};
static struct fixture f;
static const int zynq_spi_fdt_funcs, spibus_print;
static void event(char value)
{
    assert(f.trace_length + 1 < sizeof(f.trace));
    f.trace[f.trace_length++] = value;
    f.trace[f.trace_length] = '\0';
}
static void *device_private(device_t dev) { return dev->private; }
static struct clk *fdtbus_clock_get(int phandle, const char *name)
{
    assert(phandle == 42);
    if (strcmp(name, "pclk") == 0)
        return f.fail == NO_PCLK ? NULL : &f.clocks[0];
    assert(strcmp(name, "ref_clk") == 0);
    return f.fail == NO_REFCLK ? NULL : &f.clocks[1];
}
static int clk_enable(struct clk *clk)
{
    const unsigned int i = clk->index;
    assert(i < 2 && !clk->enabled);
    assert(i == 0 || f.clocks[0].enabled);
    f.enables[i]++;
    event(i == 0 ? 'P' : 'R');
    if ((i == 0 && f.fail == ENABLE_PCLK) ||
        (i == 1 && f.fail == ENABLE_REFCLK))
        return EIO;
    clk->enabled = true;
    return 0;
}
static void clk_disable(struct clk *clk)
{
    const unsigned int i = clk->index;
    assert(i < 2 && clk->enabled && !f.mapped);
    assert(i == 1 || !f.clocks[1].enabled);
    f.disables[i]++;
    clk->enabled = false;
    event(i == 0 ? 'p' : 'r');
}
static u_int clk_get_rate(struct clk *clk)
{
    assert(clk == &f.clocks[1] && clk->enabled);
    return f.fail == ZERO_RATE ? 0 : 100000000;
}
static int fdtbus_get_reg(int phandle, unsigned index, bus_addr_t *addr,
    bus_size_t *size)
{
    assert(phandle == 42 && index == 0);
    if (f.fail == NO_REG)
        return ENOENT;
    *addr = 0x12340000;
    *size = f.fail == SHORT_REG ? 0x2c : 0x30;
    return 0;
}
static int bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
    assert(tag == &f && addr == 0x12340000 && size == 0x30 && flags == 0);
    assert(f.clocks[0].enabled && f.clocks[1].enabled && !f.mapped);
    f.maps++;
    event('M');
    if (f.fail == MAP)
        return ENOMEM;
    f.mapped = true;
    *handle = 0x56780000;
    return 0;
}
static void bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t size)
{
    assert(tag == &f && handle == 0x56780000 && size == 0x30);
    assert(f.mapped && f.clocks[0].enabled && f.clocks[1].enabled);
    assert(f.regs[ZSPI_ENABLE / 4] == 0);
    f.mapped = false;
    f.unmaps++;
    event('U');
}
static void mmio_check(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t reg)
{
    assert(tag == &f && handle == 0x56780000);
    assert(f.mapped && f.clocks[0].enabled && f.clocks[1].enabled);
    assert(reg < 0x30 && reg % 4 == 0);
}
static uint32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t reg)
{
    mmio_check(tag, handle, reg);
    if (reg == ZSPI_STATUS)
        return f.fail == DRAIN ? ZSPI_RX_NOT_EMPTY : 0;
    if (reg == ZSPI_RX_DATA)
        f.rx_reads++;
    return f.regs[reg / 4];
}
static void bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t reg, uint32_t value)
{
    mmio_check(tag, handle, reg);
    f.regs[reg / 4] = value;
}
static void bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, bus_size_t length, int flags)
{
    mmio_check(tag, handle, offset);
    assert(length == 0x30);
    assert(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
}
static void aprint_error(const char *message) { (void)message; f.errors++; }
static void aprint_naive(const char *message) { (void)message; }
static void aprint_normal(const char *format, u_int rate)
{ (void)format; assert(rate == 100000000); }
static int zynq_spi_configure(void *cookie, int slave, int mode, int speed)
{ (void)cookie; (void)slave; (void)mode; (void)speed; return 0; }
static int zynq_spi_transfer(void *cookie, struct spi_transfer *transfer)
{ (void)cookie; (void)transfer; return 0; }
static void fdtbus_register_spi_controller(device_t dev, int phandle,
    const int *funcs)
{
    assert(f.mapped && f.clocks[0].enabled && f.clocks[1].enabled);
    assert(phandle == 42 && funcs == &zynq_spi_fdt_funcs);
    assert(dev->private->sc_spi.sct_cookie == dev->private);
    assert(dev->private->sc_spi.sct_nslaves == ZSPI_NSLAVES);
    assert(dev->private->sc_spi.sct_configure == zynq_spi_configure);
    assert(dev->private->sc_spi.sct_transfer == zynq_spi_transfer);
    assert(f.registered == 0 && f.attached == 0);
    f.registered++;
    event('C');
}
static void fdtbus_attach_spibus(device_t dev, int phandle, int print)
{
    assert(dev->private->sc_dev == dev && phandle == 42 && print == spibus_print);
    assert(f.registered == 1 && f.attached == 0);
    f.attached++;
    event('S');
}
/* ACTUAL_DRIVER */

int main(void)
{
    static const struct {
        enum failure failure;
        const char *trace;
        unsigned int enables[2], disables[2], maps, unmaps;
    } cases[] = {
        {NONE,          "PRMCS",  {1, 1}, {0, 0}, 1, 0},
        {NO_PCLK,       "",       {0, 0}, {0, 0}, 0, 0},
        {NO_REFCLK,     "",       {0, 0}, {0, 0}, 0, 0},
        {ENABLE_PCLK,   "P",      {1, 0}, {0, 0}, 0, 0},
        {ENABLE_REFCLK, "PRp",    {1, 1}, {1, 0}, 0, 0},
        {ZERO_RATE,     "PRrp",   {1, 1}, {1, 1}, 0, 0},
        {NO_REG,        "PRrp",   {1, 1}, {1, 1}, 0, 0},
        {SHORT_REG,     "PRrp",   {1, 1}, {1, 1}, 0, 0},
        {MAP,           "PRMrp",  {1, 1}, {1, 1}, 1, 0},
        {DRAIN,         "PRMUrp", {1, 1}, {1, 1}, 1, 1}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct zynq_spi_softc sc = {0};
        struct device dev = {.private = &sc};
        struct fdt_attach_args args = {.faa_phandle = 42, .faa_bst = &f};
        memset(&f, 0, sizeof(f));
        f.clocks[1].index = 1;
        f.fail = cases[i].failure;
        zynq_spi_attach(NULL, &dev, &args);
        if (strcmp(f.trace, cases[i].trace) != 0) {
            fprintf(stderr, "case %zu: expected %s, got %s\n", i,
                cases[i].trace, f.trace);
            return 1;
        }
        assert(memcmp(f.enables, cases[i].enables, sizeof(f.enables)) == 0);
        assert(memcmp(f.disables, cases[i].disables, sizeof(f.disables)) == 0);
        assert(f.maps == cases[i].maps && f.unmaps == cases[i].unmaps);
        const bool success = f.fail == NONE;
        assert(f.clocks[0].enabled == success && f.clocks[1].enabled == success);
        assert(f.mapped == success);
        assert(f.registered == (unsigned int)success);
        assert(f.attached == (unsigned int)success);
        assert(f.errors == (unsigned int)!success);
        assert(f.rx_reads == (f.fail == DRAIN ? ZSPI_FIFO_DEPTH : 0));
    }
    puts("Zynq SPI attach clock/MMIO lifecycle: 10 cases passed");
    return 0;
}
