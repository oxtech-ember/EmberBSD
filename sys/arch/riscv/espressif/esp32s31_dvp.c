/* $NetBSD$ */
/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * ESP32-S31 DVP capture path. Register sequence and signal routing follow
 * Espressif ESP-IDF v6.1 esp_driver_cam/dvp and esp_hal_cam/esp32s31
 * (Apache-2.0 OR MIT). DVP is the FDT graph/AXI-DMA consumer; sensor mode
 * programming remains in the sensor subdevice.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/workqueue.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/fdt_port.h>
#include <dev/fdt/syscon.h>
#include <dev/video_if.h>
#include <dev/video_sensor_if.h>

#include "esp32s31_dvp_lifecycle.h"

#define DVP_CAM_CTRL		0x004
#define DVP_CAM_CTRL1		0x008
#define DVP_CAM_STOP_EN		__BIT(0)
#define DVP_CAM_VS_EOF_EN	__BIT(8)
#define DVP_CAM_UPDATE_REG	__BIT(4)
#define DVP_CAM_REC_LEN		__BITS(15, 0)
#define DVP_CAM_CLK_INV		__BIT(22)
#define DVP_CAM_2BYTE		__BIT(24)
#define DVP_CAM_DE_INV		__BIT(25)
#define DVP_CAM_VSYNC_INV	__BIT(27)
#define DVP_CAM_DE_VSYNC	__BIT(28)
#define DVP_CAM_START		__BIT(29)
#define DVP_CAM_RESET		__BIT(30)
#define DVP_CAM_AFIFO_RESET	__BIT(31)

/* ESP-IDF v6.1 hp_sys_clkrst_reg.h offsets and bits. */
#define HP_LCDCAM_CTRL0		0x14c
#define HP_LCDCAM_LCDCAM_CTRL0	0x154
#define HP_LCDCAM_CAM_CTRL0	0x158
#define HP_LCDCAM_APB_CLK	__BIT(0)
#define HP_LCDCAM_APB_RESET	__BIT(1)
#define HP_LCDCAM_SYS_CLK	__BIT(2)
#define HP_LCDCAM_RESET	__BIT(3)
#define HP_LCDCAM_CAM_CLK	__BIT(2)
#define HP_LCDCAM_CORE_CLK	__BIT(2)

struct esp32s31_dvp_softc {
	device_t sc_dev;
	int sc_phandle;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	bus_dma_tag_t sc_dmat;
	struct syscon *sc_syscon;
	struct fdtbus_dma *sc_dma;
	struct fdt_device_ports sc_ports;
	struct video_sensor *sc_sensor;
	device_t sc_videodev;
	struct workqueue *sc_wq;
	struct work sc_work;
	kmutex_t sc_lock;
	bus_dma_segment_t sc_seg;
	bus_dmamap_t sc_map;
	void *sc_buf;
	size_t sc_bufsize;
	struct video_format sc_format;
	struct esp32s31_dvp_lifecycle sc_lifecycle;
	uint64_t sc_work_generation;
	unsigned int sc_frameno;
};

static int esp32s31_dvp_match(device_t, cfdata_t, void *);
static void esp32s31_dvp_attach(device_t, device_t, void *);
static void esp32s31_dvp_init(device_t);
static void *esp32s31_dvp_ep_data(device_t, struct fdt_endpoint *);
static void esp32s31_dvp_dma_done(void *);
static void esp32s31_dvp_work(struct work *, void *);
static int esp32s31_dvp_start_locked(struct esp32s31_dvp_softc *);
static void esp32s31_dvp_stop_cam(struct esp32s31_dvp_softc *);
static int esp32s31_dvp_open(void *, int);
static void esp32s31_dvp_close(void *);
static const char *esp32s31_dvp_devname(void *);
static const char *esp32s31_dvp_businfo(void *);
static int esp32s31_dvp_enum_format(void *, uint32_t, struct video_format *);
static int esp32s31_dvp_get_format(void *, struct video_format *);
static int esp32s31_dvp_set_format(void *, struct video_format *);
static int esp32s31_dvp_try_format(void *, struct video_format *);
static int esp32s31_dvp_start_transfer(void *);
static int esp32s31_dvp_stop_transfer(void *);

CFATTACH_DECL_NEW(esp32s31dvp, sizeof(struct esp32s31_dvp_softc),
    esp32s31_dvp_match, esp32s31_dvp_attach, NULL, NULL);

static const struct device_compatible_entry esp32s31_dvp_compat[] = {
	{ .compat = "espressif,esp32s31-dvp" }, DEVICE_COMPAT_EOL
};

static const struct video_hw_if esp32s31_dvp_video_if = {
	.open = esp32s31_dvp_open,
	.close = esp32s31_dvp_close,
	.get_devname = esp32s31_dvp_devname,
	.get_businfo = esp32s31_dvp_businfo,
	.enum_format = esp32s31_dvp_enum_format,
	.get_format = esp32s31_dvp_get_format,
	.set_format = esp32s31_dvp_set_format,
	.try_format = esp32s31_dvp_try_format,
	.start_transfer = esp32s31_dvp_start_transfer,
	.stop_transfer = esp32s31_dvp_stop_transfer,
};

static int
esp32s31_dvp_match(device_t parent, cfdata_t match, void *aux)
{
	const struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, esp32s31_dvp_compat);
}

static void *
esp32s31_dvp_ep_data(device_t dev, struct fdt_endpoint *ep)
{
	(void)dev;
	(void)ep;
	return NULL;
}

static void
esp32s31_dvp_attach(device_t parent, device_t self, void *aux)
{
	struct esp32s31_dvp_softc *sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	bus_addr_t addr;
	bus_size_t size;
	uint32_t v;
	int error;

	(void)parent;
	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;
	sc->sc_dmat = faa->faa_dmat;
	error = fdtbus_get_reg(faa->faa_phandle, 0, &addr, &size);
	if (error != 0 || bus_space_map(sc->sc_bst, addr, size, 0,
	    &sc->sc_bsh) != 0) {
		aprint_error_dev(self, "cannot map DVP registers\n");
		return;
	}
	sc->sc_syscon = fdtbus_syscon_acquire(faa->faa_phandle,
	    "espressif,syscon");
	if (sc->sc_syscon == NULL) {
		aprint_error_dev(self, "cannot acquire clock/reset syscon\n");
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, size);
		return;
	}
	error = fdtbus_pinctrl_set_config(faa->faa_phandle, "default");
	if (error != 0) {
		aprint_error_dev(self, "cannot select DVP pins (%d)\n", error);
		bus_space_unmap(sc->sc_bst, sc->sc_bsh, size);
		return;
	}
	syscon_lock(sc->sc_syscon);
	v = syscon_read_4(sc->sc_syscon, HP_LCDCAM_CTRL0);
	v |= HP_LCDCAM_APB_CLK | HP_LCDCAM_SYS_CLK |
	    HP_LCDCAM_APB_RESET | HP_LCDCAM_RESET;
	syscon_write_4(sc->sc_syscon, HP_LCDCAM_CTRL0, v);
	v &= ~(HP_LCDCAM_APB_RESET | HP_LCDCAM_RESET);
	syscon_write_4(sc->sc_syscon, HP_LCDCAM_CTRL0, v);
	v = syscon_read_4(sc->sc_syscon, HP_LCDCAM_LCDCAM_CTRL0);
	syscon_write_4(sc->sc_syscon, HP_LCDCAM_LCDCAM_CTRL0,
	    v | HP_LCDCAM_CORE_CLK);
	v = syscon_read_4(sc->sc_syscon, HP_LCDCAM_CAM_CTRL0);
	v = (v & ~__BITS(1, 0)) | HP_LCDCAM_CAM_CLK;
	syscon_write_4(sc->sc_syscon, HP_LCDCAM_CAM_CTRL0, v);
	syscon_unlock(sc->sc_syscon);
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->sc_ports.dp_ep_get_data = esp32s31_dvp_ep_data;
	fdt_ports_register(&sc->sc_ports, self, faa->faa_phandle, EP_OTHER);
	config_interrupts(self, esp32s31_dvp_init);
	return;
}

static void
esp32s31_dvp_init(device_t self)
{
	struct esp32s31_dvp_softc *sc = device_private(self);
	struct fdt_endpoint *ep;
	int error, nsegs;

	ep = fdt_endpoint_remote_from_index(&sc->sc_ports, 0, 0);
	if (ep == NULL || fdt_endpoint_type(ep) != EP_VIDEO_SENSOR) {
		aprint_error_dev(self, "no video sensor connected in FDT graph\n");
		return;
	}
	sc->sc_sensor = fdt_endpoint_get_data(ep);
	if (sc->sc_sensor == NULL ||
	    video_sensor_get_format(sc->sc_sensor, &sc->sc_format) != 0) {
		aprint_error_dev(self, "connected sensor is not ready\n");
		return;
	}
	sc->sc_dma = fdtbus_dma_get(sc->sc_phandle, "rx",
	    esp32s31_dvp_dma_done, sc);
	if (sc->sc_dma == NULL) {
		aprint_error_dev(self, "cannot acquire AXI DMA receive channel\n");
		return;
	}
	sc->sc_bufsize = sc->sc_format.sample_size;
	if (sc->sc_bufsize == 0 || (sc->sc_bufsize & 63) != 0) {
		error = EINVAL;
		goto fail_dma;
	}
	error = bus_dmamem_alloc(sc->sc_dmat, sc->sc_bufsize, 64, 0,
	    &sc->sc_seg, 1, &nsegs, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail_dma;
	if (nsegs != 1) {
		error = EFBIG;
		goto fail_alloc;
	}
	error = bus_dmamem_map(sc->sc_dmat, &sc->sc_seg, 1, sc->sc_bufsize,
	    &sc->sc_buf, BUS_DMA_WAITOK | BUS_DMA_COHERENT);
	if (error != 0)
		goto fail_mem;
	error = bus_dmamap_create(sc->sc_dmat, sc->sc_bufsize, 1,
	    sc->sc_bufsize, 0, BUS_DMA_WAITOK, &sc->sc_map);
	if (error != 0)
		goto fail_mapping;
	error = bus_dmamap_load(sc->sc_dmat, sc->sc_map, sc->sc_buf,
	    sc->sc_bufsize, NULL, BUS_DMA_WAITOK);
	if (error != 0)
		goto fail_map;
	error = workqueue_create(&sc->sc_wq, device_xname(self),
	    esp32s31_dvp_work, sc, PRI_NONE, IPL_SOFTSERIAL, WQ_MPSAFE);
	if (error != 0)
		goto fail_loaded;
	sc->sc_videodev = video_attach_mi(&esp32s31_dvp_video_if, self, sc);
	if (sc->sc_videodev == NULL) {
		error = ENXIO;
		workqueue_destroy(sc->sc_wq);
		goto fail_loaded;
	}
	aprint_normal_dev(self, ": ESP32-S31 DVP, %ux%u frame capture\n",
	    sc->sc_format.width, sc->sc_format.height);
	return;

fail_loaded:
	bus_dmamap_unload(sc->sc_dmat, sc->sc_map);
fail_map:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_map);
fail_mapping:
	bus_dmamem_unmap(sc->sc_dmat, sc->sc_buf, sc->sc_bufsize);
fail_mem:
fail_alloc:
	bus_dmamem_free(sc->sc_dmat, &sc->sc_seg, 1);
fail_dma:
	fdtbus_dma_put(sc->sc_dma);
	aprint_error_dev(self, "cannot allocate frame buffer (%d)\n", error);
}

static void
esp32s31_dvp_dma_done(void *arg)
{
	struct esp32s31_dvp_softc *sc = arg;

	workqueue_enqueue(sc->sc_wq, &sc->sc_work, NULL);
}

static void
esp32s31_dvp_stop_cam(struct esp32s31_dvp_softc *sc)
{
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, DVP_CAM_CTRL1, 0);
}

static int
esp32s31_dvp_start_locked(struct esp32s31_dvp_softc *sc)
{
	struct fdtbus_dma_req req = { 0 };
	int error;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_map, 0, sc->sc_bufsize,
	    BUS_DMASYNC_PREREAD);
	req.dreq_segs = sc->sc_map->dm_segs;
	req.dreq_nsegs = sc->sc_map->dm_nsegs;
	req.dreq_sel = 0;
	req.dreq_dir = FDT_DMA_READ;
	error = fdtbus_dma_transfer(sc->sc_dma, &req);
	if (error != 0)
		return error;
	sc->sc_lifecycle.dma_active = true;
	esp32s31_dvp_lifecycle_transfer_started(&sc->sc_lifecycle);

	/* Reinitialize the CAM AFIFO, then listen for one VSYNC-delimited frame. */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, DVP_CAM_CTRL1,
	    DVP_CAM_RESET | DVP_CAM_AFIFO_RESET);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, DVP_CAM_CTRL1,
	    DVP_CAM_VSYNC_INV | DVP_CAM_DE_VSYNC);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, DVP_CAM_CTRL,
	    DVP_CAM_STOP_EN | DVP_CAM_VS_EOF_EN | DVP_CAM_UPDATE_REG);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, DVP_CAM_CTRL1,
	    DVP_CAM_VSYNC_INV | DVP_CAM_DE_VSYNC |
	    DVP_CAM_START);
	return 0;
}

static void
esp32s31_dvp_work(struct work *wk, void *arg)
{
	struct esp32s31_dvp_softc *sc = arg;
	struct fdtbus_dma_result result;
	struct video_payload payload;
	int error, halt_error;

	(void)wk;
	mutex_enter(&sc->sc_lock);
	if (!esp32s31_dvp_lifecycle_completion_current(&sc->sc_lifecycle,
	    sc->sc_work_generation)) {
		mutex_exit(&sc->sc_lock);
		return;
	}
	/* Stop the frame producer before inspecting or draining the DMA result. */
	esp32s31_dvp_stop_cam(sc);
	error = fdtbus_dma_result(sc->sc_dma, &result);
	if (error == 0 && result.dres_error != 0)
		error = result.dres_error;
	if (error == 0 && result.dres_bytes != sc->sc_format.sample_size)
		error = EIO;
	if (error == 0 && !esp32s31_dvp_lifecycle_result_ready(
	    &sc->sc_lifecycle, sc->sc_work_generation))
		error = EIO;
	halt_error = fdtbus_dma_halt_checked(sc->sc_dma);
	if (halt_error == 0) {
		sc->sc_lifecycle.dma_active = false;
		if (error == 0 && !esp32s31_dvp_lifecycle_dma_quiesced(
		    &sc->sc_lifecycle, sc->sc_work_generation))
			error = EIO;
	} else if (error == 0)
		error = halt_error;
	if (error != 0) {
		esp32s31_dvp_lifecycle_begin_stop(&sc->sc_lifecycle);
		esp32s31_dvp_stop_cam(sc);
		if (sc->sc_lifecycle.sensor_streaming &&
		    video_sensor_set_stream(sc->sc_sensor, false) == 0) {
			sc->sc_lifecycle.sensor_streaming = false;
		}
		(void)esp32s31_dvp_lifecycle_finish_stop(&sc->sc_lifecycle);
		aprint_error_dev(sc->sc_dev, "capture stopped (%d)\n", error);
		mutex_exit(&sc->sc_lock);
		return;
	}
	bus_dmamap_sync(sc->sc_dmat, sc->sc_map, 0, result.dres_bytes,
	    BUS_DMASYNC_POSTREAD);
	payload.data = sc->sc_buf;
	payload.size = result.dres_bytes;
	payload.frameno = sc->sc_frameno++ & 1;
	payload.end_of_frame = true;
	video_submit_payload(sc->sc_videodev, &payload);
	if (esp32s31_dvp_lifecycle_rearm_allowed(&sc->sc_lifecycle,
	    sc->sc_work_generation)) {
		error = esp32s31_dvp_start_locked(sc);
		if (error != 0) {
			esp32s31_dvp_lifecycle_begin_stop(&sc->sc_lifecycle);
			esp32s31_dvp_stop_cam(sc);
			if (video_sensor_set_stream(sc->sc_sensor, false) == 0)
				sc->sc_lifecycle.sensor_streaming = false;
			(void)esp32s31_dvp_lifecycle_finish_stop(&sc->sc_lifecycle);
			aprint_error_dev(sc->sc_dev,
			    "cannot restart capture (%d)\n", error);
		}
	}
	mutex_exit(&sc->sc_lock);
}

static int
esp32s31_dvp_open(void *arg, int flags)
{
	(void)arg;
	(void)flags;
	return 0;
}
static void esp32s31_dvp_close(void *arg) { (void)esp32s31_dvp_stop_transfer(arg); }
static const char *esp32s31_dvp_devname(void *arg)
{
	return device_xname(((struct esp32s31_dvp_softc *)arg)->sc_dev);
}
static const char *esp32s31_dvp_businfo(void *arg) { (void)arg; return "fdt"; }
static int
esp32s31_dvp_enum_format(void *arg, uint32_t index, struct video_format *fmt)
{
	if (index != 0)
		return EINVAL;
	return esp32s31_dvp_get_format(arg, fmt);
}
static int
esp32s31_dvp_get_format(void *arg, struct video_format *fmt)
{
	struct esp32s31_dvp_softc *sc = arg;
	return video_sensor_get_format(sc->sc_sensor, fmt);
}
static int
esp32s31_dvp_try_format(void *arg, struct video_format *fmt)
{
	struct esp32s31_dvp_softc *sc = arg;
	return video_sensor_try_format(sc->sc_sensor, fmt);
}
static int
esp32s31_dvp_set_format(void *arg, struct video_format *fmt)
{
	struct esp32s31_dvp_softc *sc = arg;
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_lifecycle.streaming || sc->sc_lifecycle.stopping)
		error = EBUSY;
	else {
		error = video_sensor_try_format(sc->sc_sensor, fmt);
		if (error == 0 && fmt->sample_size != sc->sc_bufsize)
			error = ENOTSUP;
		if (error == 0)
			error = video_sensor_set_format(sc->sc_sensor, fmt);
		if (error == 0)
			sc->sc_format = *fmt;
	}
	mutex_exit(&sc->sc_lock);
	return error;
}
static int
esp32s31_dvp_start_transfer(void *arg)
{
	struct esp32s31_dvp_softc *sc = arg;
	int error;

	mutex_enter(&sc->sc_lock);
	if (sc->sc_lifecycle.streaming || sc->sc_lifecycle.stopping) {
		mutex_exit(&sc->sc_lock);
		return EBUSY;
	}
	if (!esp32s31_dvp_lifecycle_start_allowed(&sc->sc_lifecycle)) {
		mutex_exit(&sc->sc_lock);
		return EBUSY;
	}
	error = video_sensor_set_format(sc->sc_sensor, &sc->sc_format);
	if (error == 0)
		error = video_sensor_set_stream(sc->sc_sensor, true);
	if (error == 0) {
		sc->sc_lifecycle.sensor_streaming = true;
		sc->sc_work_generation =
		    esp32s31_dvp_lifecycle_begin_stream(&sc->sc_lifecycle);
		error = esp32s31_dvp_start_locked(sc);
	}
	if (error != 0) {
		esp32s31_dvp_lifecycle_begin_stop(&sc->sc_lifecycle);
		esp32s31_dvp_stop_cam(sc);
		if (sc->sc_lifecycle.sensor_streaming &&
		    video_sensor_set_stream(sc->sc_sensor, false) == 0) {
			sc->sc_lifecycle.sensor_streaming = false;
		}
		(void)esp32s31_dvp_lifecycle_finish_stop(&sc->sc_lifecycle);
	}
	mutex_exit(&sc->sc_lock);
	return error;
}
static int
esp32s31_dvp_stop_transfer(void *arg)
{
	struct esp32s31_dvp_softc *sc = arg;
	bool dma_active;
	int error = 0;

	mutex_enter(&sc->sc_lock);
	if (!esp32s31_dvp_lifecycle_stop_acquire(&sc->sc_lifecycle)) {
		mutex_exit(&sc->sc_lock);
		return EBUSY;
	}
	esp32s31_dvp_lifecycle_begin_stop(&sc->sc_lifecycle);
	esp32s31_dvp_stop_cam(sc);
	dma_active = sc->sc_lifecycle.dma_active;
	mutex_exit(&sc->sc_lock);
	if (dma_active) {
		error = fdtbus_dma_halt_checked(sc->sc_dma);
	}
	if (error == 0 && dma_active) {
		mutex_enter(&sc->sc_lock);
		sc->sc_lifecycle.dma_active = false;
		mutex_exit(&sc->sc_lock);
	}
	workqueue_wait(sc->sc_wq, &sc->sc_work);
	mutex_enter(&sc->sc_lock);
	if (sc->sc_lifecycle.sensor_streaming) {
		int e = video_sensor_set_stream(sc->sc_sensor, false);
		if (error == 0)
			error = e;
		if (e == 0)
			sc->sc_lifecycle.sensor_streaming = false;
	}
	if (error == 0)
		(void)esp32s31_dvp_lifecycle_finish_stop(&sc->sc_lifecycle);
	esp32s31_dvp_lifecycle_stop_release(&sc->sc_lifecycle);
	mutex_exit(&sc->sc_lock);
	return error;
}
