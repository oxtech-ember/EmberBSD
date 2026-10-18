/* Origin: EmberBSD; AI-assisted actual NPU/FDT attach controls. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned long bus_addr_t;
typedef unsigned long bus_size_t;
typedef int devmajor_t;
struct sun60i_npu_softc {
	void *sc_dev;
	int sc_phandle, sc_bst, sc_run_lock;
	bool sc_run_pass, sc_cdev_attached;
	devmajor_t sc_cmajor;
};
typedef struct sun60i_npu_softc *device_t;
struct fdt_attach_args { int faa_phandle, faa_bst; };
struct fdt_node {
	bool n_pinctrl_init;
	int n_phandle;
	void *n_bus;
	const char *n_name;
};
static struct sun60i_npu_softc *sun60i_npu_instance;
static devmajor_t sun60i_npu_cmajor = -1;
static int sun60i_npu_cdevsw;
static unsigned sysctls, registrations, power_calls, pin_calls;
static bool managed = true;
static int property_size;
#define A733_NPU_BASE 0x03600000
#define A733_NPU_ID_SIZE 0x34
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define device_private(d) (d)
#define mutex_init(m, a, b) (*(m) = 1)
#define aprint_error_dev(...) ((void)0)
#define aprint_debug_dev(...) ((void)0)
#define aprint_naive(...) ((void)0)
#define aprint_normal(...) ((void)0)

static int
fdtbus_get_reg(int node, int index, bus_addr_t *addr, bus_size_t *size)
{
	assert(node == 17 && index == 0);
	*addr = A733_NPU_BASE;
	*size = 0x1000;
	return 0;
}
static void
sun60i_npu_sysctl_setup(const struct sun60i_npu_softc *sc)
{
	assert(sc == sun60i_npu_instance);
	sysctls++;
}
static int
devsw_attach(const char *name, const void *b, const devmajor_t *bm, const void *c,
    devmajor_t *cm)
{
	assert(strcmp(name, "vipcore") == 0 && b == NULL && *bm == -1);
	assert(c == &sun60i_npu_cdevsw && *cm == -1);
	*cm = 368;
	registrations++;
	return 0;
}
static bool
fdtbus_pinctrl_has_config(int node, const char *name)
{
	assert(node == 17 && strcmp(name, "init") == 0);
	return false;
}
static int
fdtbus_pinctrl_set_config(int node, const char *name)
{
	assert(node == 17 && strcmp(name, "default") == 0);
	pin_calls++;
	return ENOENT;
}
static bool
of_hasprop(int node, const char *name)
{
	assert(node == 17);
	return strcmp(name, "netbsd,consumer-managed-power") == 0 ?
	    managed : strcmp(name, "power-domains") == 0;
}
static int
OF_getproplen(int node, const char *name)
{
	assert(node == 17 &&
	    strcmp(name, "netbsd,consumer-managed-power") == 0);
	return property_size;
}
static int
fdtbus_powerdomain_enable_on_attach(int node)
{
	assert(node == 17);
	power_calls++;
	return 0;
}
#include "npu-attach.h"
#include "fdt-attach.h"

int
main(void)
{
	static struct sun60i_npu_softc sc;
	struct fdt_attach_args args = { 17, 3 };
	struct fdt_node node = { .n_phandle = 17, .n_name = "npu" };
	int error;

	error = fdt_pre_attach(&node);
	assert(error == 0);
	assert(power_calls == 0 && pin_calls == 1);
	/* No pinctrl property exists on the NPU DT node. */
	property_size = 4;
	error = fdt_pre_attach(&node);
	assert(error == EINVAL && power_calls == 0);
	managed = false;
	error = fdt_pre_attach(&node);
	assert(error == 0 && power_calls == 1);
	sun60i_npu_attach(NULL, &sc, &args);
	assert(sc.sc_dev == &sc && sc.sc_phandle == 17 && sc.sc_bst == 3);
	assert(sc.sc_run_pass && sc.sc_run_lock == 1);
	assert(sysctls == 1 && registrations == 1);
	assert(sc.sc_cdev_attached && sc.sc_cmajor == 368);
	puts("PASS: actual-source FDT managed power and NPU attach; no MMIO API");
	return 0;
}
