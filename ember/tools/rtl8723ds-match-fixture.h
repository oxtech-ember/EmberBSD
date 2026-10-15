/* Origin: EmberBSD - model cached NetBSD SDIO/FDT attachment inputs. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Only the cached fields consumed by the real match function are modeled. */
typedef struct match_device *device_t;
typedef void *cfdata_t;
typedef struct { int type, integer; } devhandle_t;
#define DEVHANDLE_TYPE_OF 0x4f504657
#define SFF_ERROR 0x0001
#define SMF_IO_MODE 0x0004
#define SMF_MEM_MODE 0x0008
struct match_device {
	const char *name;
	device_t parent;
	devhandle_t handle;
};
struct sdmmc_cis { uint16_t manufacturer, product; };
struct sdmmc_function;
struct sdmmc_softc {
	device_t sc_dev;
	uint32_t sc_flags;
	struct sdmmc_function *sc_fn0;
};
struct sdmmc_function {
	struct sdmmc_softc *sc;
	int flags, number;
	struct sdmmc_cis cis;
};
struct sdmmc_attach_args {
	uint16_t manufacturer, product;
	int interface;
	struct sdmmc_function *sf;
};
static const void *match_fdt;
static device_t device_parent(device_t dev) { return dev->parent; }
static bool device_is_a(device_t dev, const char *name) {
	return strcmp(dev->name, name) == 0;
}
static devhandle_t device_handle(device_t dev) { return dev->handle; }
static int devhandle_type(devhandle_t handle) { return handle.type; }
static int devhandle_to_of(devhandle_t handle) {
	assert(handle.type == DEVHANDLE_TYPE_OF);
	return handle.integer;
}
static int fdtbus_phandle2offset(int phandle) { return phandle - 1; }
static const void *fdtbus_get_data(void) { return match_fdt; }
#include "native-match.inc"
