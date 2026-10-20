/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual native constructor/IRQ/bind bodies with controlled kernel services.
 */
#define main vblank_reference_main
#include "bcmv3d-vblank-contract.c"
#undef main
#include <unistd.h>
#include <sys/wait.h>
#define KM_SLEEP 1
#define MUTEX_DEFAULT 1
#define IPL_VM 1
#define ACPI_LEVEL_SENSITIVE 1
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define KASSERT CHECK
#define ASSERT_SLEEPABLE() CHECK(!in_irq)
typedef void *bus_space_tag_t;
typedef unsigned bus_space_handle_t;
typedef struct {
	bool live, held;
	int ipl;
} kmutex_t;
struct drm_device {
	unsigned refs;
};
struct bcmv3d_mmu4_native {
	int unused;
};
struct bcmv3d_vblank_native;
struct bcmv3d_device {
	struct drm_device *drm;
	void *dev;
	bus_space_tag_t bst;
	struct bcmv3d_mmu4_native *mmu4_native;
	struct bcmv3d_vblank_native *vblank_native;
	kmutex_t memory_lock;
};
#define BCMV3D_MMU4_NATIVE_COMPLETE 7
struct bcmv3d_mmu4_native_status {
	int stage, fault, program_error, prepare_error, operation_error,
	    close_error, status_error;
	bool sealed, backing_valid;
};
struct acpi_irq {
	unsigned ar_irq;
	int ar_type;
};
#include "display-types.h"
#include "native-types.h"
static struct registers regs;
static struct drm_device drm;
static struct bcmv3d_device device;
static struct bcmv3d_mmu4_native mmu;
static bool in_irq, establish_fail, irq_returned;
static unsigned masks, establishes, allocations, notifies, fault_notifies;
static int irq_token;
static int (*handler)(void *);
static void *handler_arg;
static struct bcmv3d_mmu4_native_status mmu_status;
static int
cpu_intr_p(void)
{
	return in_irq;
}
static int
cpu_softintr_p(void)
{
	return 0;
}
static void
mutex_init(kmutex_t *m, int type, int ipl)
{
	CHECK(type == MUTEX_DEFAULT);
	m->live = true;
	m->held = false;
	m->ipl = ipl;
}
static bool
mutex_owned(kmutex_t *m)
{
	CHECK(m->live);
	return m->held;
}
static void
mutex_enter(kmutex_t *m)
{
	CHECK(m->live && !m->held);
	m->held = true;
}
static void
mutex_exit(kmutex_t *m)
{
	CHECK(m->live && m->held);
	m->held = false;
}
static void
mutex_destroy(kmutex_t *m)
{
	CHECK(m->live && !m->held);
	m->live = false;
}
static void *
kmem_zalloc(size_t size, int flags)
{
	void *p;
	CHECK(flags == KM_SLEEP && !in_irq);
	p = calloc(1, size);
	CHECK(p != NULL);
	allocations++;
	return p;
}
static void
kmem_free(void *p, size_t size)
{
	CHECK(p && size && allocations);
	allocations--;
	free(p);
}
static void
drm_dev_get(struct drm_device *d)
{
	CHECK(d == &drm);
	d->refs++;
}
static int
bcmv3d_mmu4_native_cached_status(
    struct bcmv3d_mmu4_native *n, struct bcmv3d_mmu4_native_status *out)
{
	CHECK(n == &mmu && !device.memory_lock.held);
	*out = mmu_status;
	return 0;
}
static int
bus_space_peek_4(
    bus_space_tag_t t, bus_space_handle_t h, uint32_t o, uint32_t *v)
{
	CHECK(!in_irq && t == &device && (h == 1 || h == 2));
	return read_reg(
	    &regs, h == 1 ? BCMV3D_VBLANK_PV0 : BCMV3D_VBLANK_HVS, o, v);
}
static int
bus_space_poke_4(
    bus_space_tag_t t, bus_space_handle_t h, uint32_t o, uint32_t v)
{
	CHECK(!in_irq && t == &device && h == 1);
	return write_reg(&regs, BCMV3D_VBLANK_PV0, o, v);
}
static uint32_t
bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, uint32_t o)
{
	uint32_t v;
	CHECK(in_irq && t == &device && (h == 1 || h == 2));
	CHECK(read_reg(&regs, h == 1 ? BCMV3D_VBLANK_PV0 : BCMV3D_VBLANK_HVS,
		  o, &v) == 0);
	return v;
}
static void
bus_space_write_4(
    bus_space_tag_t t, bus_space_handle_t h, uint32_t o, uint32_t v)
{
	CHECK(in_irq && t == &device && h == 1);
	CHECK(write_reg(&regs, BCMV3D_VBLANK_PV0, o, v) == 0);
}
static void
bus_space_barrier(bus_space_tag_t t, bus_space_handle_t h, unsigned o,
    unsigned len, int flags)
{
	CHECK(t == &device && h == 1 && o == 0 && len == 0x100 && flags == 3);
	barrier_reg(&regs, BCMV3D_VBLANK_PV0);
}
static void *
acpi_intr_establish_irq(void *dev, struct acpi_irq *irq, int ipl, bool mpsafe,
    int (*fn)(void *), void *arg, const char *name)
{
	unsigned before = regs.calls;
	CHECK(dev == &device && irq->ar_irq == 133 &&
	      irq->ar_type == ACPI_LEVEL_SENSITIVE);
	CHECK(ipl == IPL_VM && mpsafe && strcmp(name, "bcmv3d-pv0") == 0);
	CHECK(regs.enabled == 0 && (regs.pending & 0x80) == 0);
	establishes++;
	handler = fn;
	handler_arg = arg;
	/* A real establishment may dispatch before returning its handle. */
	CHECK(handler(arg) == 0 && regs.calls == before && masks == 0);
	irq_returned = true;
	return establish_fail ? NULL : &irq_token;
}
static void
acpi_intr_mask(void *ih)
{
	CHECK(ih == &irq_token && irq_returned);
	masks++;
}
#include "native-body.h"
static void
notify(void *arg, const struct bcmv3d_vblank_snapshot *s)
{
	CHECK(arg == &device && in_irq && !device.vblank_native->lock.held);
	CHECK(s->version == 1 && s->length == sizeof(*s));
	if (s->first_error != 0) {
		CHECK(
		    s->first_error == EBUSY && s->sequence == 0 && !s->routed);
		CHECK(s->last_window == BCMV3D_VBLANK_HVS &&
		      s->last_offset == 0x100);
		CHECK(s->raw_valid && s->last_raw == 0 &&
		      s->reject_mask == 0x80000000U);
		fault_notifies++;
		return;
	}
	CHECK(s->sequence == 1);
	CHECK(s->active_list == 42 && s->frame_count == 0 &&
	      s->first_error == 0);
	notifies++;
}
static void
scenario(unsigned which)
{
	struct bcm2712_display_contract c = {
	    .version = BCM2712_DISPLAY_VERSION};
	struct bcmv3d_vblank_native *n = (void *)(uintptr_t)1;
	struct bcmv3d_vblank_snapshot snap;
	unsigned before;
	int error;
	regs = initial(0x54);
	mutex_init(&device.memory_lock, MUTEX_DEFAULT, 0);
	device.dev = &device;
	device.bst = &device;
	device.drm = &drm;
	device.mmu4_native = &mmu;
	mmu_status = (struct bcmv3d_mmu4_native_status){
	    .stage = BCMV3D_MMU4_NATIVE_COMPLETE,
	    .sealed = true,
	    .backing_valid = true};
	c.irq[2] = 133;
	c.memory[3] = (struct bcm2712_display_memory){0x107c580000, 0x1a000};
	c.memory[5] = (struct bcm2712_display_memory){0x107c410000, 0x100};
	if (which == 1) {
		in_irq = true;
		CHECK(bcmv3d_vblank_native_create(
			  &device, &c, 1, 2, 0x54, &n) == EWOULDBLOCK);
		CHECK(allocations == 0 && regs.calls == 0);
		return;
	}
	if (which == 2) {
		c.irq[2]++;
		CHECK(bcmv3d_vblank_native_create(
			  &device, &c, 1, 2, 0x54, &n) == ENXIO &&
		      allocations == 0);
		return;
	}
	if (which == 3) {
		mmu_status.fault = EIO;
		CHECK(bcmv3d_vblank_native_create(
			  &device, &c, 1, 2, 0x54, &n) == EBUSY &&
		      allocations == 0);
		return;
	}
	if (which == 8) {
		mutex_enter(&device.memory_lock);
		CHECK(bcmv3d_vblank_native_create(
			  &device, &c, 1, 2, 0x54, &n) == EDEADLK &&
		      allocations == 0 && regs.calls == 0);
		return;
	}
	if (which == 4)
		regs.fail = 1;
	if (which == 5)
		establish_fail = true;
	if (which == 6)
		regs.fail = 11; /* arm write after ten prepare operations. */
	error = bcmv3d_vblank_native_create(&device, &c, 1, 2, 0x54, &n);
	if (which >= 4 && which <= 6) {
		CHECK(error == (which == 5 ? ENXIO : EIO) &&
		      n == (void *)(uintptr_t)1);
		CHECK(device.vblank_native != NULL && drm.refs == 1 &&
		      allocations == 1);
		CHECK(masks == (which == 6 ? 1U : 0U));
		CHECK(bcmv3d_vblank_native_snapshot(
			  device.vblank_native, &snap) == 0 &&
		      snap.first_error == (unsigned)error);
		return;
	}
	CHECK(error == 0 && n == device.vblank_native && establishes == 1 &&
	      regs.enabled == 0x80);
	CHECK(regs.barriers == 3);
	CHECK(bcmv3d_vblank_native_bind(n, notify, &device) == 0);
	CHECK(bcmv3d_vblank_native_bind(n, notify, &device) == EBUSY);
	before = regs.calls;
	CHECK(
	    bcmv3d_vblank_native_create(&device, &c, 1, 2, 0x54, &n) == EBUSY);
	CHECK(regs.calls == before && allocations == 1 && drm.refs == 1);
	in_irq = true;
	CHECK(handler(handler_arg) == 0 && notifies == 0);
	regs.pending = 0x81;
	regs.status = 0x4000;
	regs.active = 42;
	if (which == 7) {
		regs.control = 0;
		before = regs.calls;
		CHECK(handler(handler_arg) == 1 && masks == 1 &&
		      regs.calls == before + 3 && notifies == 0 &&
		      fault_notifies == 1);
		CHECK(handler(handler_arg) == 0 && regs.calls == before + 3);
	} else {
		CHECK(handler(handler_arg) == 1 && notifies == 1 &&
		      regs.pending == 1);
		CHECK(regs.barriers == 4);
	}
	in_irq = false;
	CHECK(bcmv3d_vblank_native_snapshot(n, &snap) == 0);
	CHECK(snap.sequence == (which == 7 ? 0U : 1U));
}
int
main(void)
{
	unsigned which;
	int status;
	pid_t pid;
	for (which = 0; which < 9; which++) {
		pid = fork();
		CHECK(pid >= 0);
		if (pid == 0) {
			scenario(which);
			_Exit(0);
		}
		CHECK(waitpid(pid, &status, 0) == pid);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			return 1;
	}
	puts("PASS actual PV0 native owner:9 isolated boots, "
	     "spurious-before-handle, callback outside lock, sticky failure "
	     "retention");
	return 0;
}
