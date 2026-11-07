/*-
 * Copyright (c) 2026 EmberBSD contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD$");

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/flashio.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/fdt/fdt_boot.h>
#include <dev/fdt/fdtvar.h>
#include <dev/flash/flash.h>
#include <dev/flash/flash_io.h>

#include <machine/sbi.h>

#define SBI_EID_VENDOR_START	0x09000000
#define SBI_EID_VENDOR_MASK	0x00ffffff

#define S31_SBI_FLASH_GETINFO	6
#define S31_SBI_FLASH_READ	7
#define S31_SBI_FLASH_WRITE	8
#define S31_SBI_FLASH_ERASE	9
#define S31_SBI_FLASH_SYNC	10

#define S31_FLASH_INFO_SIZE	0
#define S31_FLASH_INFO_ERASESIZE	1
#define S31_FLASH_INFO_PAGESIZE	2
#define S31_FLASH_INFO_WRITESIZE	3

#define S31_FLASH_MAX_TRANSFER	4096
#define S31_FLASH_WRITE_ALIGN	4
#define S31_FLASH_PSRAM_START	0x50000000U
#define S31_FLASH_PSRAM_SIZE	0x04000000U

struct esp32s31_flash_softc {
	device_t		 sc_dev;
	struct flash_interface sc_if;
	struct flash_io	 sc_io;
	kmutex_t		 sc_lock;
	bus_dma_tag_t		 sc_dmat;
	bus_dma_segment_t	 sc_seg;
	bus_dmamap_t		 sc_map;
	void			*sc_buf;
	bool			 sc_mem_allocated;
	bool			 sc_mem_mapped;
	bool			 sc_map_created;
	bool			 sc_map_loaded;
	int			 sc_eid;
	uint32_t		 sc_size;
	bool			 sc_io_ready;
};

static const struct device_compatible_entry esp32s31_flash_compat[] = {
	{ .compat = "espressif,esp32s31-nor-sbi" },
	DEVICE_COMPAT_EOL
};

static int esp32s31_flash_match(device_t, cfdata_t, void *);
static void esp32s31_flash_attach(device_t, device_t, void *);
static int esp32s31_flash_detach(device_t, int);

CFATTACH_DECL_NEW(esp32s31flash, sizeof(struct esp32s31_flash_softc),
	 esp32s31_flash_match, esp32s31_flash_attach, esp32s31_flash_detach,
	 NULL);

static int
esp32s31_flash_error(long error)
{
	switch (error) {
	case SBI_SUCCESS:
		return 0;
	case SBI_ERR_INVALID_PARAM:
		return EINVAL;
	case SBI_ERR_INVALID_ADDRESS:
		return EFAULT;
	case SBI_ERR_ALREADY_STARTED:
		return EBUSY;
	case SBI_ERR_NOT_SUPPORTED:
		return EOPNOTSUPP;
	default:
		return EIO;
	}
}

static int
esp32s31_flash_info(struct esp32s31_flash_softc *sc, uint32_t selector,
	uint32_t *value)
{
	struct sbiret ret;

	ret = SBI_CALL1(sc->sc_eid, S31_SBI_FLASH_GETINFO, selector);
	if (ret.error != SBI_SUCCESS)
		return esp32s31_flash_error(ret.error);
	if (ret.value <= 0 || (uint64_t)ret.value > UINT32_MAX)
		return EIO;
	*value = (uint32_t)ret.value;
	return 0;
}

static int
esp32s31_flash_alloc_dma(struct esp32s31_flash_softc *sc,
	bus_dma_tag_t dmat)
{
	bus_size_t size = S31_FLASH_MAX_TRANSFER;
	bus_addr_t pa;
	int error, nsegs;

	sc->sc_dmat = dmat;
	error = bus_dmamem_alloc(dmat, size, PAGE_SIZE, 0, &sc->sc_seg, 1,
	    &nsegs, BUS_DMA_WAITOK);
	if (error)
		return error;
	sc->sc_mem_allocated = true;
	if (nsegs != 1) {
		error = EFBIG;
		goto fail;
	}
	error = bus_dmamem_map(dmat, &sc->sc_seg, 1, size, &sc->sc_buf,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT);
	if (error)
		goto fail;
	sc->sc_mem_mapped = true;
	error = bus_dmamap_create(dmat, size, 1, size, 0, BUS_DMA_WAITOK,
	    &sc->sc_map);
	if (error)
		goto fail;
	sc->sc_map_created = true;
	error = bus_dmamap_load(dmat, sc->sc_map, sc->sc_buf, size, NULL,
	    BUS_DMA_WAITOK);
	if (error)
		goto fail;
	sc->sc_map_loaded = true;
	if (sc->sc_map->dm_nsegs != 1) {
		error = EFBIG;
		goto fail;
	}
	pa = sc->sc_map->dm_segs[0].ds_addr;
	if (pa < S31_FLASH_PSRAM_START || size > S31_FLASH_PSRAM_SIZE ||
	    pa - S31_FLASH_PSRAM_START > S31_FLASH_PSRAM_SIZE - size) {
		error = EFAULT;
		goto fail;
	}
	return 0;

fail:
	if (sc->sc_map_loaded) {
		bus_dmamap_unload(dmat, sc->sc_map);
		sc->sc_map_loaded = false;
	}
	if (sc->sc_map_created) {
		bus_dmamap_destroy(dmat, sc->sc_map);
		sc->sc_map_created = false;
	}
	if (sc->sc_mem_mapped) {
		bus_dmamem_unmap(dmat, sc->sc_buf, size);
		sc->sc_mem_mapped = false;
		sc->sc_buf = NULL;
	}
	if (sc->sc_mem_allocated) {
		bus_dmamem_free(dmat, &sc->sc_seg, 1);
		sc->sc_mem_allocated = false;
	}
	return error;
}

static void
esp32s31_flash_free_dma(struct esp32s31_flash_softc *sc)
{
	if (sc->sc_map_loaded) {
		bus_dmamap_unload(sc->sc_dmat, sc->sc_map);
		sc->sc_map_loaded = false;
	}
	if (sc->sc_map_created) {
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_map);
		sc->sc_map_created = false;
	}
	if (sc->sc_mem_mapped) {
		bus_dmamem_unmap(sc->sc_dmat, sc->sc_buf,
		    S31_FLASH_MAX_TRANSFER);
		sc->sc_mem_mapped = false;
		sc->sc_buf = NULL;
	}
	if (sc->sc_mem_allocated) {
		bus_dmamem_free(sc->sc_dmat, &sc->sc_seg, 1);
		sc->sc_mem_allocated = false;
	}
}

static void
esp32s31_flash_dma_sync(struct esp32s31_flash_softc *sc, size_t length,
	int ops)
{
	bus_dmamap_sync(sc->sc_dmat, sc->sc_map, 0, length, ops);
}

static int
esp32s31_flash_call(struct esp32s31_flash_softc *sc, long func,
	uint32_t offset, uint32_t length)
{
	struct sbiret ret;
	bus_addr_t pa = sc->sc_map->dm_segs[0].ds_addr;

	ret = SBI_CALL3(sc->sc_eid, func, offset, pa, length);
	return esp32s31_flash_error(ret.error);
}

static int
esp32s31_flash_read(device_t self, flash_off_t offset, size_t length,
	size_t *retlen, uint8_t *buffer)
{
	struct esp32s31_flash_softc *sc = device_private(self);
	size_t done = 0;
	int error = 0;

	if (retlen != NULL)
		*retlen = 0;
	if (offset < 0 || (uint64_t)offset > sc->sc_size ||
	    (uint64_t)length > sc->sc_size - (uint64_t)offset ||
	    (uint64_t)length > (uint64_t)INT64_MAX - (uint64_t)offset)
		return EINVAL;
	if (length == 0)
		return 0;
	mutex_enter(&sc->sc_lock);
	while (done < length) {
		flash_off_t at = offset + done;
		size_t count;
		uint32_t aligned;

		if ((at & (S31_FLASH_WRITE_ALIGN - 1)) != 0 ||
		    length - done < S31_FLASH_WRITE_ALIGN) {
			aligned = at & ~(S31_FLASH_WRITE_ALIGN - 1);
			esp32s31_flash_dma_sync(sc, S31_FLASH_WRITE_ALIGN,
			    BUS_DMASYNC_PREREAD);
			error = esp32s31_flash_call(sc, S31_SBI_FLASH_READ,
			    (uint32_t)aligned, sizeof(uint32_t));
			esp32s31_flash_dma_sync(sc, S31_FLASH_WRITE_ALIGN,
			    BUS_DMASYNC_POSTREAD);
			if (error)
				break;
			count = MIN(length - done,
			    (size_t)S31_FLASH_WRITE_ALIGN -
			    (size_t)(at - aligned));
			memcpy(buffer + done,
			    (const uint8_t *)sc->sc_buf + (at - aligned), count);
		} else {
			count = MIN(length - done, S31_FLASH_MAX_TRANSFER);
			count &= ~(S31_FLASH_WRITE_ALIGN - 1);
			esp32s31_flash_dma_sync(sc, count, BUS_DMASYNC_PREREAD);
			error = esp32s31_flash_call(sc, S31_SBI_FLASH_READ,
			    (uint32_t)at, count);
			esp32s31_flash_dma_sync(sc, count, BUS_DMASYNC_POSTREAD);
			if (error)
				break;
			memcpy(buffer + done, sc->sc_buf, count);
		}
		done += count;
	}
	mutex_exit(&sc->sc_lock);
	if (retlen != NULL)
		*retlen = done;
	return error;
}

static int
esp32s31_flash_write(device_t self, flash_off_t offset, size_t length,
	size_t *retlen, const uint8_t *buffer)
{
	struct esp32s31_flash_softc *sc = device_private(self);
	size_t done = 0;
	int error = 0;

	if (retlen != NULL)
		*retlen = 0;
	if (offset < 0 || (uint64_t)offset > sc->sc_size ||
	    (uint64_t)length > sc->sc_size - (uint64_t)offset ||
	    (uint64_t)length > (uint64_t)INT64_MAX - (uint64_t)offset ||
	    (offset & (S31_FLASH_WRITE_ALIGN - 1)) != 0 ||
	    (length & (S31_FLASH_WRITE_ALIGN - 1)) != 0)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	while (done < length) {
		size_t count = MIN(length - done, S31_FLASH_MAX_TRANSFER);

		count &= ~(S31_FLASH_WRITE_ALIGN - 1);
		memcpy(sc->sc_buf, buffer + done, count);
		esp32s31_flash_dma_sync(sc, count, BUS_DMASYNC_PREWRITE);
		error = esp32s31_flash_call(sc, S31_SBI_FLASH_WRITE,
		    (uint32_t)(offset + done), count);
		esp32s31_flash_dma_sync(sc, count, BUS_DMASYNC_POSTWRITE);
		if (error)
			break;
		done += count;
	}
	mutex_exit(&sc->sc_lock);
	if (retlen != NULL)
		*retlen = done;
	return error;
}

static int
esp32s31_flash_erase(device_t self, struct flash_erase_instruction *ei)
{
	struct esp32s31_flash_softc *sc = device_private(self);
	flash_off_t done = 0;
	int error = 0;

	ei->ei_state = FLASH_ERASE_FAILED;
	if (ei->ei_addr < 0 || ei->ei_len < 0 ||
	    (uint64_t)ei->ei_addr > sc->sc_size ||
	    (uint64_t)ei->ei_len > sc->sc_size - (uint64_t)ei->ei_addr ||
	    ei->ei_addr % sc->sc_if.erasesize != 0 ||
	    ei->ei_len % sc->sc_if.erasesize != 0)
		return EINVAL;
	mutex_enter(&sc->sc_lock);
	while (done < ei->ei_len) {
		uint32_t count = MIN(ei->ei_len - done, S31_FLASH_MAX_TRANSFER);

		count -= count % sc->sc_if.erasesize;
		if (count == 0) {
			error = EINVAL;
			break;
		}
		error = esp32s31_flash_call(sc, S31_SBI_FLASH_ERASE,
		    (uint32_t)(ei->ei_addr + done), count);
		if (error)
			break;
		done += count;
	}
	if (error == 0)
		ei->ei_state = FLASH_ERASE_DONE;
	mutex_exit(&sc->sc_lock);
	if (error == 0 && ei->ei_callback != NULL)
		ei->ei_callback(ei);
	return error;
}

static int
esp32s31_flash_sync(device_t self)
{
	struct esp32s31_flash_softc *sc = device_private(self);
	struct sbiret ret;
	int error;

	error = flash_io_sync(&sc->sc_io);
	if (error)
		return error;
	mutex_enter(&sc->sc_lock);
	ret = SBI_CALL0(sc->sc_eid, S31_SBI_FLASH_SYNC);
	mutex_exit(&sc->sc_lock);
	return esp32s31_flash_error(ret.error);
}

static int
esp32s31_flash_isbad(device_t self, flash_off_t offset, bool *bad)
{
	(void)self;
	(void)offset;
	*bad = false;
	return 0;
}

static int
esp32s31_flash_markbad(device_t self, flash_off_t offset)
{
	(void)self;
	(void)offset;
	return EOPNOTSUPP;
}

static int
esp32s31_flash_submit(device_t self, struct buf *bp)
{
	struct esp32s31_flash_softc *sc = device_private(self);

	return flash_io_submit(&sc->sc_io, bp);
}

static int
esp32s31_flash_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args *faa = aux;

	(void)parent;
	(void)cf;

	return of_compatible_match(faa->faa_phandle, esp32s31_flash_compat);
}

static void
esp32s31_flash_attach(device_t parent, device_t self, void *aux)
{
	struct esp32s31_flash_softc *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	uint32_t erasesize, pagesize, writesize;
	struct sbiret vendor;
	int error;

	(void)parent;
	(void)aux;

	sc->sc_dev = self;
	vendor = sbi_get_mvendorid();
	if (vendor.error != SBI_SUCCESS) {
		aprint_error_dev(self, "cannot query SBI vendor ID\n");
		return;
	}
	sc->sc_eid = SBI_EID_VENDOR_START +
	    ((uint32_t)vendor.value & SBI_EID_VENDOR_MASK);
	error = esp32s31_flash_info(sc, S31_FLASH_INFO_SIZE, &sc->sc_size);
	if (error == 0)
		error = esp32s31_flash_info(sc, S31_FLASH_INFO_ERASESIZE,
		    &erasesize);
	if (error == 0)
		error = esp32s31_flash_info(sc, S31_FLASH_INFO_PAGESIZE,
		    &pagesize);
	if (error == 0)
		error = esp32s31_flash_info(sc, S31_FLASH_INFO_WRITESIZE,
		    &writesize);
	if (error || sc->sc_size == 0 || erasesize == 0 || pagesize == 0 ||
	    writesize != S31_FLASH_WRITE_ALIGN ||
	    erasesize > S31_FLASH_MAX_TRANSFER ||
	    erasesize % pagesize != 0 ||
	    (sc->sc_size & (S31_FLASH_WRITE_ALIGN - 1)) != 0) {
		aprint_error_dev(self, "flash SBI provider unavailable or invalid geometry\n");
		return;
	}
	error = esp32s31_flash_alloc_dma(sc, faa->faa_dmat);
	if (error) {
		aprint_error_dev(self, "cannot allocate PSRAM DMA buffer: %d\n",
		    error);
		return;
	}
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_if.read = esp32s31_flash_read;
	sc->sc_if.write = esp32s31_flash_write;
	sc->sc_if.erase = esp32s31_flash_erase;
	sc->sc_if.sync = esp32s31_flash_sync;
	sc->sc_if.block_isbad = esp32s31_flash_isbad;
	sc->sc_if.block_markbad = esp32s31_flash_markbad;
	sc->sc_if.submit = esp32s31_flash_submit;
	sc->sc_if.page_size = pagesize;
	sc->sc_if.erasesize = erasesize;
	sc->sc_if.writesize = writesize;
	sc->sc_if.type = FLASH_TYPE_NOR;

	error = flash_sync_thread_init(&sc->sc_io, self, &sc->sc_if);
	if (error) {
		aprint_error_dev(self, "cannot initialize flash I/O thread: %d\n",
		    error);
		mutex_destroy(&sc->sc_lock);
		esp32s31_flash_free_dma(sc);
		return;
	}
	sc->sc_io_ready = true;

	aprint_normal(": SBI NOR, %u bytes, erase %u, page %u, program %u\n",
	    sc->sc_size, erasesize, pagesize, writesize);
	flash_attach_mtdparts(&sc->sc_if, self, sc->sc_size, "espnor0",
	    fdt_get_bootargs());
}

static int
esp32s31_flash_detach(device_t self, int flags)
{
	struct esp32s31_flash_softc *sc = device_private(self);
	int error;

	error = config_detach_children(self, flags);
	if (error)
		return error;

	if (sc->sc_io_ready) {
		flash_sync_thread_destroy(&sc->sc_io);
		mutex_destroy(&sc->sc_lock);
	}
	esp32s31_flash_free_dma(sc);
	return 0;
}
