/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8822CE fake native bus API, AI-assisted. */
#ifndef RTW88_TEST_KERNEL_H
#define RTW88_TEST_KERNEL_H
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define KASSERT(x) assert(x)
#define KM_NOSLEEP 0
#define BUS_DMA_NOWAIT 1
#define BUS_DMA_COHERENT 2
#define BUS_DMASYNC_PREREAD 1
#define BUS_DMASYNC_PREWRITE 2
#define BUS_DMASYNC_POSTREAD 4
#define BUS_DMASYNC_POSTWRITE 8
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define MUTEX_DEFAULT 0
#define IPL_NET 1
#define DVF_DETACH_SHUTDOWN 0
#define PCI_ID_REG 0
#define PCI_COMMAND_STATUS_REG 4
#define PCI_COMMAND_MASTER_ENABLE 4
#define PCI_COMMAND_MEM_ENABLE 2
#define PCI_BAR(n) (0x10 + 4 * (n))
#define PCI_MAPREG_TYPE_MEM 0
#define PCI_MAPREG_TYPE(x) ((x) & 1)
#define PCI_VENDOR(x) ((x) & 0xffff)
#define PCI_PRODUCT(x) ((x) >> 16)
#define PCI_INTR_TYPE_INTX 0
#define PCI_INTR_TYPE_MSI 1
#define PCI_INTR_TYPE_MSIX 2
#define PCI_INTR_TYPE_SIZE 3
#define CFATTACH_DECL3_NEW(n, sz, m, a, d, x, y, z, f)                         \
	const struct fake_cf n = {m, a, d}

typedef void *device_t;
typedef void *cfdata_t;
typedef uint32_t pcireg_t;
typedef int pci_chipset_tag_t;
typedef int pcitag_t;
typedef int pci_intr_handle_t;
typedef int pci_intr_type_t;
typedef int bus_space_tag_t;
typedef int bus_space_handle_t;
typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef void *bus_dma_tag_t;
typedef struct {
	int initialized, held;
} kmutex_t;
typedef struct {
	bus_addr_t ds_addr;
	bus_size_t ds_len;
} bus_dma_segment_t;
struct fake_map {
	int dm_nsegs;
	bus_size_t dm_mapsize;
	bus_dma_segment_t dm_segs[1];
	int loaded;
};
typedef struct fake_map *bus_dmamap_t;
struct pci_attach_args {
	pci_chipset_tag_t pa_pc;
	pcitag_t pa_tag;
	pcireg_t pa_id;
	bus_dma_tag_t pa_dmat;
};
struct fake_cf {
	int (*match)(device_t, cfdata_t, void *);
	void (*attach)(device_t, device_t, void *);
	int (*detach)(device_t, int);
};
void *device_private(device_t);
const char *device_xname(device_t);
void aprint_normal_dev(device_t, const char *, ...);
void aprint_error_dev(device_t, const char *, ...);
void aprint_normal(const char *, ...);
void *kmem_zalloc(size_t, int);
void kmem_free(void *, size_t);
void mutex_init(kmutex_t *, int, int);
void mutex_destroy(kmutex_t *);
void mutex_enter(kmutex_t *);
void mutex_exit(kmutex_t *);
void delay(unsigned);
void xc_barrier(unsigned);
pcireg_t pci_conf_read(pci_chipset_tag_t, pcitag_t, int);
void pci_conf_write(pci_chipset_tag_t, pcitag_t, int, pcireg_t);
pcireg_t pci_mapreg_type(pci_chipset_tag_t, pcitag_t, int);
int pci_mapreg_map(const struct pci_attach_args *, int, pcireg_t, int,
		   bus_space_tag_t *, bus_space_handle_t *, bus_addr_t *,
		   bus_size_t *);
void bus_space_unmap(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint8_t bus_space_read_1(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint16_t bus_space_read_2(bus_space_tag_t, bus_space_handle_t, bus_size_t);
uint32_t bus_space_read_4(bus_space_tag_t, bus_space_handle_t, bus_size_t);
void bus_space_write_1(bus_space_tag_t, bus_space_handle_t, bus_size_t,
		       uint8_t);
void bus_space_write_2(bus_space_tag_t, bus_space_handle_t, bus_size_t,
		       uint16_t);
void bus_space_write_4(bus_space_tag_t, bus_space_handle_t, bus_size_t,
		       uint32_t);
void bus_space_barrier(bus_space_tag_t, bus_space_handle_t, bus_size_t,
		       bus_size_t, int);
int bus_dmatag_subregion(bus_dma_tag_t, bus_addr_t, bus_addr_t, bus_dma_tag_t *,
			 int);
void bus_dmatag_destroy(bus_dma_tag_t);
int bus_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t, bus_size_t,
		      int, bus_dmamap_t *);
void bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
int bus_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t, bus_size_t,
		     bus_dma_segment_t *, int, int *, int);
void bus_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
int bus_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *, int, size_t, void **,
		   int);
void bus_dmamem_unmap(bus_dma_tag_t, void *, size_t);
int bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t, void *,
		    int);
void bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
void bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t, bus_size_t, int);
typedef void *firmware_handle_t;
int firmware_open(const char *, const char *, firmware_handle_t *);
int firmware_close(firmware_handle_t);
long firmware_get_size(firmware_handle_t);
int firmware_read(firmware_handle_t, long, void *, size_t);
int pci_intr_alloc(const struct pci_attach_args *, pci_intr_handle_t **, int *,
		   pci_intr_type_t);
void pci_intr_release(pci_chipset_tag_t, pci_intr_handle_t *, int);
void *pci_intr_establish_xname(pci_chipset_tag_t, pci_intr_handle_t, int,
			       int (*)(void *), void *, const char *);
void pci_intr_disestablish(pci_chipset_tag_t, void *);
#endif
