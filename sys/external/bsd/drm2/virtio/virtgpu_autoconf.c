/* Origin: EmberBSD native VirtGPU attachment, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/cdefs.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/virtiovar.h>
#include <drm/drm_drv.h>
#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>
#include "virtgpu_drv.h"
#include "locators.h"
#include "opt_virtgpu_virgl.h"

struct virtiodrm_softc {
	device_t sc_dev;
	struct virtio_device sc_vdev;
	struct netbsd_virtqueue *sc_vqs[2];
	struct drm_device *sc_drm;
};
static int virtiodrm_match(device_t, cfdata_t, void *);
static void virtiodrm_attach(device_t, device_t, void *);
static void virtiodrm_attach_deferred(device_t);
static int virtiodrm_detach(device_t, int);
CFATTACH_DECL_NEW(virtiodrm, sizeof(struct virtiodrm_softc),
    virtiodrm_match, virtiodrm_attach, virtiodrm_detach, NULL);

static int
virtiodrm_match(device_t parent, cfdata_t cf, void *aux)
{
	struct virtio_attach_args *va = aux;

	return va->sc_childdevid == VIRTIO_DEVICE_ID_GPU &&
	    virtio_child(device_private(parent)) == NULL ? 2 : 0;
}

static int
virtiodrm_config_bridge(struct virtio_softc *vsc)
{
	struct virtiodrm_softc *sc = device_private(virtio_child(vsc));

	return linux_virtio_config_interrupt(&sc->sc_vdev);
}

static void
virtiodrm_config_changed(struct virtio_device *vdev)
{
	struct drm_device *dev = vdev->priv;
	struct virtio_gpu_device *vgdev = dev->dev_private;

	schedule_work(&vgdev->config_changed_work);
}

static void
virtiodrm_attach(device_t parent, device_t self, void *aux)
{
	static vq_callback_t *callbacks[] = {
		virtio_gpu_ctrl_ack, virtio_gpu_cursor_ack
	};
	static const char * const names[] = { "control", "cursor" };
	struct virtiodrm_softc *sc = device_private(self);
	struct virtio_softc *vsc = device_private(parent);
	uint64_t features = 0;
	int error;

	aprint_naive("\n");
#ifdef VIRTGPU_VIRGL
	features = UINT64_C(1) << VIRTIO_GPU_F_VIRGL;
	aprint_normal(": experimental VirtIO GPU DRM (classic VirGL requested)\n");
#else
	aprint_normal(": experimental VirtIO GPU DRM (2D only)\n");
#endif
	sc->sc_dev = self;
	/* EDID, blob and context-init remain outside this opt-in. */
	virtio_child_attach_start(vsc, self, IPL_VM, features,
	    VIRTIO_COMMON_FLAG_BITS);
	if (!virtio_version_1(vsc)) {
		aprint_error_dev(self, "requires modern VirtIO\n");
		virtio_child_attach_failed(vsc);
		return;
	}
	error = linux_virtio_init(&sc->sc_vdev, vsc, self, IPL_VM,
	    1024 * 1024, virtiodrm_config_bridge, virtiodrm_config_changed,
	    virtio_gpu_cancel_vbuf);
	if (error) {
		aprint_error_dev(self, "transport initialization: %d\n", error);
		virtio_child_attach_failed(vsc);
		return;
	}
	/* Native parent requires queues/interrupts before attach returns. */
	error = virtio_find_vqs(&sc->sc_vdev, __arraycount(sc->sc_vqs),
	    sc->sc_vqs, callbacks, names, NULL);
	if (error) {
		aprint_error_dev(self, "queue initialization: %d\n", error);
		linux_virtio_fini(&sc->sc_vdev);
		virtio_child_attach_failed(vsc);
		return;
	}
	config_interrupts(self, virtiodrm_attach_deferred);
}

static int
virtiodrm_set_busid(struct drm_device *dev)
{
	device_t transport = device_parent(dev->dev);
	device_t bus = device_parent(transport);
	struct pci_softc *psc;
	struct drm_native_pci_record *r = &dev->native_pci;
	pcitag_t tag;
	pcireg_t id, class, subsystem;
	int d, f;
	char busid[40];

	if (!bus || !device_is_a(bus, "pci"))
		return drm_dev_set_unique(dev, device_xname(dev->dev));
	psc = device_private(bus);
	/* Actual locators exist before attach, even if c_dev is unfilled. */
	d = device_locator(transport, PCICF_DEV);
	f = device_locator(transport, PCICF_FUNCTION);
	if (d < 0 || d >= 32 || f < 0 || f >= 8)
		return -ENODEV;
	/* Snapshot public identity once, using the actual transport locators. */
	tag = pci_make_tag(psc->sc_pc, psc->sc_bus, d, f);
	id = pci_conf_read(psc->sc_pc, tag, PCI_ID_REG);
	class = pci_conf_read(psc->sc_pc, tag, PCI_CLASS_REG);
	subsystem = pci_conf_read(psc->sc_pc, tag, PCI_SUBSYS_ID_REG);
	if (PCI_VENDOR(id) == 0xffff || PCI_VENDOR(id) == 0)
		return -ENODEV;
	r->version = DRM_NATIVE_PCI_VERSION;
	r->length = sizeof(*r);
	r->flags = DRM_NATIVE_REVISION;
	r->bus_type = DRM_NATIVE_BUS_PCI;
	r->domain = pci_get_segment(psc->sc_pc);
	r->bus = psc->sc_bus;
	r->device = d;
	r->function = f;
	r->vendor = PCI_VENDOR(id);
	r->product = PCI_PRODUCT(id);
	r->subvendor = PCI_SUBSYS_VENDOR(subsystem);
	r->subproduct = PCI_SUBSYS_ID(subsystem);
	r->revision = PCI_REVISION(class);
	snprintf(busid, sizeof(busid), "pci:%04x:%02x:%02x.%d",
	    pci_get_segment(psc->sc_pc), psc->sc_bus, d, f);
	aprint_normal_dev(dev->dev, "DRM bus ID %s\n", busid);
	return drm_dev_set_unique(dev, busid);
}

static void
virtiodrm_attach_deferred(device_t self)
{
	struct virtiodrm_softc *sc = device_private(self);
	struct drm_device *dev;
	extern int drm_guarantee_initialized(void);
	int ret;

	ret = -drm_guarantee_initialized();
	if (ret)
		goto failed;
	dev = drm_dev_alloc(&virtio_gpu_driver, self);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto failed;
	}
	dev->bus_dmat = dev->bus_dmat32 = dev->dmat = sc->sc_vdev.dmat;
	sc->sc_vdev.priv = dev;
	ret = virtiodrm_set_busid(dev);
	if (ret)
		goto put;
	ret = virtio_gpu_init(dev, &sc->sc_vdev, sc->sc_vqs);
	if (ret)
		goto put;
	ret = drm_dev_register(dev, 0);
	if (ret) {
		virtio_gpu_deinit(dev);
		goto put;
	}
	sc->sc_drm = dev;
	ret = virtgpu_console_init(dev);
	if (ret)
		aprint_error_dev(self, "console unavailable: %d\n", ret);
	return;
put:
	drm_dev_put(dev);
failed:
	aprint_error_dev(self, "DRM initialization: %d\n", ret);
	linux_virtio_fini(&sc->sc_vdev);
	virtio_child_attach_failed(sc->sc_vdev.native);
}

static int
virtiodrm_detach(device_t self, int flags)
{
	/* Hot removal/unload is deliberately unsupported in this experiment. */
	return EBUSY;
}
