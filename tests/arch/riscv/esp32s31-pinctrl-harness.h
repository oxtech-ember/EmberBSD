/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h, l) ((UINT32_MAX >> (31 - ((h) - (l)))) << (l))
#define __SHIFTIN(v, m) ((v) << __builtin_ctz(m))
#define be32toh(v) __builtin_bswap32(v)
static uint32_t
be32dec(const void *p)
{
	uint32_t v;
	memcpy(&v, p, 4);
	return be32toh(v);
}
typedef void *device_t;
typedef int bus_space_tag_t;
typedef unsigned int bus_space_handle_t;
typedef unsigned int bus_size_t;
typedef int kmutex_t;
#define device_private(x) (x)
static void
mutex_enter(kmutex_t *x)
{
	(void)x;
}
static void
mutex_exit(kmutex_t *x)
{
	(void)x;
}
static uint32_t reg[2][1024];
static unsigned int writes;
static uint32_t
bus_space_read_4(int tag, unsigned int handle, unsigned int off)
{
	(void)tag;
	assert(handle < 2 && off / 4 < 1024);
	return reg[handle][off / 4];
}
static void
bus_space_write_4(int tag, unsigned int handle, unsigned int off, uint32_t val)
{
	(void)tag;
	assert(handle < 2 && off / 4 < 1024);
	reg[handle][off / 4] = val;
	writes++;
}
static uint32_t cells[2];
static unsigned int ncells, props;
enum {
	INPUT = 1,
	OUTPUT = 2,
	PULLUP = 4,
	PULLDOWN = 8,
	OD = 16,
	HIGH = 32,
	LOW = 64,
	DISIN = 128,
	DISOUT = 256,
	PUSH = 512,
	NOBIAS = 1024
};
static int
fdtbus_get_phandle_from_native(uint32_t x)
{
	return (int)x;
}
static int
OF_child(int x)
{
	return x == 100 ? 1 : 0;
}
static int
OF_peer(int x)
{
	(void)x;
	return 0;
}
static const void *
fdtbus_get_prop(int g, const char *name, int *len)
{
	assert(g == 1);
	assert(!strcmp(name, "pinmux"));
	*len = ncells * 4;
	return cells;
}
static int
of_hasprop(int g, const char *name)
{
	(void)g;
	struct {
		const char *name;
		int bit;
	} p[] = {{"input-enable", INPUT},    {"output-enable", OUTPUT},
	         {"bias-pull-up", PULLUP},   {"bias-pull-down", PULLDOWN},
	         {"drive-open-drain", OD},   {"output-high", HIGH},
	         {"output-low", LOW},        {"input-disable", DISIN},
	         {"output-disable", DISOUT}, {"drive-push-pull", PUSH},
	         {"bias-disable", NOBIAS}};
	for (unsigned int i = 0; i < sizeof(p) / sizeof(p[0]); i++)
		if (!strcmp(name, p[i].name))
			return !!(props & p[i].bit);
	return 0;
}
#include "esp32s31_gpio_logic.h"
