/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual adapter/provider/cache/sequencer plus a real backing service bridge. */
#define bus_space_map icac_bus_space_map
#define bus_space_read_4 icac_bus_space_read_4
#define bus_space_write_4 icac_bus_space_write_4
#define bus_space_barrier icac_bus_space_barrier
#define acpi_match_node icac_match_node
#include "icac-services.h"
#undef acpi_match_node
#undef bus_space_map
#undef bus_space_read_4
#undef bus_space_write_4
#undef bus_space_barrier

#include <signal.h>
#include <stddef.h>

typedef size_t bus_size_t;
typedef void *bus_dma_tag_t;
typedef struct { uint64_t ds_addr, ds_len; } bus_dma_segment_t;
typedef void vmem_t;
typedef uint64_t vmem_addr_t;
struct drm_file;
struct uvm_pagerops;
struct drm_gem_object { int unused; };
struct drm_gem_cma_object { struct drm_gem_object base; };
struct drm_driver { const char *name; };
struct drm_device;
struct drm_minor { int type; struct drm_device *dev; };
struct drm_device {
	void *dev_private;
	device_t dev;
	bool registered;
	struct drm_driver *driver;
	struct drm_minor *render;
};
#define DRM_MINOR_RENDER 2
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define to_drm_gem_cma_obj(p) container_of(p, struct drm_gem_cma_object, base)
#define KM_SLEEP 0
#include "bo-stats.h"
#include "drv-types.h"
#include "backing-types.h"
#include "adapter-types.h"

struct cfattach { const char *ca_name; };
typedef struct cfattach *cfattach_t;
static struct cfattach gpu_attachment = { "bcmv3d_acpi" };
static struct fixture_device gpu_device;
static struct acpi_devnode gpu_node;
static struct drm_driver render_driver;
static struct drm_minor render_minor;
static struct drm_device render_device;
static struct bcmv3d_device render_sc;
static unsigned int drm_gets, drm_puts, heap_allocs, heap_frees;
static unsigned int local_maps, local_reads, local_writes, local_barriers;
static unsigned int snapshots, observes, backing_creates;
static unsigned int snapshot_failure, backing_failure;
static unsigned int backing_snapshots, backing_snapshot_failure;
static int local_map_error;
static bool race_observation, fail_pages, fail_close, race_slot;
static bool reenter_io, fail_shared;
static uint32_t debug_value, local_control, shoot_value, misc_value;
static uint32_t last_write;
static size_t last_offset;
static bool pending;
static struct bcmv3d_mmu4_native *owner, *foreign_native_pointer;

int native_backing_create(uint32_t, void **, unsigned int);
int native_backing_prepare(void *, const struct bcm2712_mmu4_config *, bool);
void native_backing_check(void *);

static void mutex_destroy(kmutex_t *lock)
{ CHECK(lock->initialized && !lock->held); lock->initialized = false; }
static void drm_dev_get(struct drm_device *dev)
{ CHECK(dev == &render_device); drm_gets++; }
static void drm_dev_put(struct drm_device *dev)
{ CHECK(dev == &render_device); drm_puts++; }
static void *kmem_zalloc(size_t size, int flags)
{
	void *p;
	CHECK(flags == KM_SLEEP && !bcm2712_iommuc_mutex.held);
	p = calloc(1, size); CHECK(p != NULL); heap_allocs++;
	if (race_slot)
		render_sc.mmu4_native = foreign_native_pointer;
	return p;
}
static void kmem_free(void *p, size_t size)
{ CHECK(p != NULL && size != 0); heap_frees++; free(p); }
static cfattach_t device_cfattach(device_t dev)
{ return dev == &gpu_device ? &gpu_attachment : NULL; }
static struct acpi_devnode *acpi_match_node(ACPI_HANDLE handle)
{ return handle == GPU ? &gpu_node : icac_match_node(handle); }

static int adapter_cached(struct bcm2712_iommuc_provider *, struct bcm2712_iommuc_native_status *);
static int adapter_observe(struct bcm2712_iommuc_provider *);
static int adapter_backing_create(struct bcmv3d_device *, uint32_t, struct bcmv3d_mmu4_backing **);
static int adapter_pages(void *, const struct bcm2712_mmu4_config *);
static int adapter_backing_status(const struct bcmv3d_mmu4_backing *,
    struct bcmv3d_mmu4_backing_status *);
static int adapter_fini(struct bcm2712_mmu4_core *);
static int adapter_shared(struct bcm2712_iommuc_core *, struct bcm2712_iommuc_status *);
static int bus_space_map(bus_space_tag_t, uint64_t, uint64_t, int,
    bus_space_handle_t *);
static uint32_t bus_space_read_4(bus_space_tag_t, bus_space_handle_t, size_t);
static void bus_space_write_4(bus_space_tag_t, bus_space_handle_t, size_t, uint32_t);
static void bus_space_barrier(bus_space_tag_t, bus_space_handle_t, size_t, size_t, int);
#define bcm2712_iommuc_cached_status adapter_cached
#define bcm2712_iommuc_observe_control adapter_observe
#define bcmv3d_mmu4_backing_create adapter_backing_create
#define bcmv3d_mmu4_backing_prepare adapter_pages
#define bcmv3d_mmu4_backing_status adapter_backing_status
#define bcm2712_mmu4_core_fini adapter_fini
#define bcm2712_iommuc_core_status adapter_shared
#include "adapter-body.h"
#undef bcm2712_mmu4_core_fini
#undef bcm2712_iommuc_core_status
#undef bcmv3d_mmu4_backing_status
#undef bcmv3d_mmu4_backing_prepare
#undef bcmv3d_mmu4_backing_create
#undef bcm2712_iommuc_observe_control
#undef bcm2712_iommuc_cached_status

static struct bcmv3d_mmu4_native foreign_native;

static int adapter_cached(struct bcm2712_iommuc_provider *sc,
    struct bcm2712_iommuc_native_status *out)
{
	CHECK(render_sc.mmu4_native != NULL && render_sc.mmu4_native->lock.held);
	if (++snapshots == snapshot_failure)
		return EAGAIN;
	return bcm2712_iommuc_cached_status(sc, out);
}
static int adapter_observe(struct bcm2712_iommuc_provider *sc)
{
	observes++;
	if (race_observation)
		CHECK(bcm2712_iommuc_observe_control(sc) == 0);
	return bcm2712_iommuc_observe_control(sc);
}
static int adapter_backing_create(struct bcmv3d_device *sc, uint32_t debug,
    struct bcmv3d_mmu4_backing **out)
{
	void *p = NULL;
	int error;
	CHECK(sc == &render_sc && owner->lock.held && !bcm2712_iommuc_mutex.held);
	CHECK(local_maps == 1 && local_reads == 1 && debug == debug_value);
	backing_creates++;
	error = native_backing_create(debug, &p, backing_failure);
	if (error == 0)
		*out = p;
	return error;
}
static int adapter_pages(void *arg, const struct bcm2712_mmu4_config *cfg)
{
	CHECK(owner->lock.held && bcm2712_iommuc_mutex.held);
	CHECK(owner->status.sealed && owner->executing);
	return native_backing_prepare(arg, cfg, fail_pages);
}
static int
adapter_backing_status(const struct bcmv3d_mmu4_backing *b,
    struct bcmv3d_mmu4_backing_status *out)
{
	CHECK(owner->lock.held && !bcm2712_iommuc_mutex.held);
	if (++backing_snapshots == backing_snapshot_failure)
		return EAGAIN;
	return bcmv3d_mmu4_backing_status(b, out);
}

static int adapter_shared(struct bcm2712_iommuc_core *cache,
    struct bcm2712_iommuc_status *out)
{
	if (fail_shared)
		return EIO;
	return bcm2712_iommuc_core_status(cache,out);
}
static int adapter_fini(struct bcm2712_mmu4_core *sequence)
{
	CHECK(owner->status.sequence_valid);
	CHECK(owner->status.sequence.reservation_owned == sequence->status.reservation_owned);
	if (fail_close)
		sequence->cache->status.retained |= 2;
	return bcm2712_mmu4_core_fini(sequence);
}

static int
bus_space_map(bus_space_tag_t tag, uint64_t pa, uint64_t size, int flags,
    bus_space_handle_t *out)
{
	CHECK(owner == render_sc.mmu4_native && owner->lock.held);
	CHECK(!bcm2712_iommuc_mutex.held);
	CHECK(tag == &tag_token && pa == UINT64_C(0x1000005200) && size == 0x80);
	CHECK(flags == 0 && out != NULL && ++local_maps == 1);
	CHECK(owner->status.icac_valid && owner->status.icac.observation.stage ==
	    BCM2712_IOMMUC_OBSERVE_COMPLETE);
	if (local_map_error != 0)
		return local_map_error;
	*out = 0x4567;
	return 0;
}
static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, size_t offset)
{
	CHECK(owner->lock.held && tag == &tag_token && handle == 0x4567);
	CHECK(offset <= 0x7c && (offset & 3) == 0 && !pending);
	local_reads++;
	if (local_reads == 1) {
		CHECK(offset == 0x38 && !bcm2712_iommuc_mutex.held);
		CHECK(owner->status.prepare_attempted && !owner->status.debug_valid);
		return debug_value;
	}
	CHECK(owner->status.sealed && owner->executing && bcm2712_iommuc_mutex.held);
	CHECK(owner->active != NULL && owner->cache == &storage.core);
	CHECK(owner->status.fault == 0 && owner->active->status.fault == 0);
	if (reenter_io) {
		struct bcmv3d_mmu4_native_status status;
		unsigned int before = calls;
		reenter_io = false;
		CHECK(bcmv3d_mmu4_native_prepare(owner) == EDEADLK);
		CHECK(bcmv3d_mmu4_native_program(owner) == EDEADLK);
		CHECK(bcmv3d_mmu4_native_cached_status(owner, &status) == EDEADLK);
		CHECK(calls == before);
	}
	switch (offset) {
	case 0x38: return debug_value;
	case 0: return local_writes == 0 ? local_control :
	    local_writes < 8 ? 0 : UINT32_C(0x04090803);
	case 0x18: return shoot_value;
	case 0x24: return misc_value;
	default: CHECK(false); return 0;
	}
}
static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle, size_t offset,
    uint32_t value)
{
	static const size_t offsets[] = {0,0x24,0x14,0x1c,0x20,4,0x30,0};
	CHECK(owner->lock.held && bcm2712_iommuc_mutex.held);
	CHECK(tag == &tag_token && handle == 0x4567 && !pending);
	CHECK(owner->status.write_attempted && owner->status.sealed);
	CHECK(owner->active->status.mutation_attempted && owner->active->status.retained);
	CHECK((storage.core.status.reserved & 2) && (storage.core.status.retained & 2));
	CHECK(owner->status.fault == 0 && owner->active->status.fault == 0);
	CHECK(local_writes < 8 && offset == offsets[local_writes]);
	switch (local_writes) {
	case 0: CHECK(value == 0x0810100c); break;
	case 1: CHECK(value == (misc_value & ~UINT32_C(0x80000000))); break;
	case 2: CHECK(value == 0x80000000); break;
	case 3: case 4: CHECK(value == 0); break;
	case 5: CHECK(value == (owner->cfg.top_pa >> 12)); break;
	case 6: CHECK(value == (0x80000000U | (owner->cfg.default_pa >> 12))); break;
	case 7: CHECK(value == 0x04090803 && writes == 1 && barriers == 1); break;
	}
	local_writes++; last_write = value; last_offset = offset; pending = true;
}
static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle, size_t offset,
    size_t length, int flags)
{
	CHECK(owner->lock.held && bcm2712_iommuc_mutex.held);
	CHECK(tag == &tag_token && handle == 0x4567 && offset == 0 && length == 0x80);
	CHECK(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
	CHECK(pending && ++local_barriers == local_writes);
	pending = false;
}

static void
adapter_setup(void)
{
	foreign_native_pointer = &foreign_native;
	native_setup();
	attach();
	memset(&gpu_device,0,sizeof(gpu_device));
	memset(&render_sc,0,sizeof(render_sc));
	memset(&render_device,0,sizeof(render_device));
	memset(&render_minor,0,sizeof(render_minor));
	gpu_device.driver = "bcmv3d"; gpu_device.private = &render_sc;
	gpu_node.ad_handle = GPU; gpu_node.ad_type = ACPI_TYPE_DEVICE;
	gpu_node.ad_device = &gpu_device;
	render_driver.name = "v3d";
	render_device.dev_private = &render_sc; render_device.dev = &gpu_device;
	render_device.registered = true; render_device.driver = &render_driver;
	render_device.render = &render_minor;
	render_minor.dev = &render_device; render_minor.type = DRM_MINOR_RENDER;
	render_sc.dev = &gpu_device; render_sc.drm = &render_device;
	render_sc.bst = &tag_token; render_sc.dmat = &tag_token; render_sc.handle = GPU;
	mutex_init(&render_sc.memory_lock,MUTEX_DEFAULT,IPL_NONE);
	debug_value = 0x20006664; misc_value = 0xa5a5a5a5;
}
static void
make_owner(void)
{
	struct bcmv3d_mmu4_native *out = &foreign_native;
	CHECK(bcmv3d_mmu4_native_create(&render_sc,&out) == 0);
	owner = out;
	CHECK(owner == render_sc.mmu4_native && drm_gets == 1 && drm_puts == 0);
	CHECK(heap_allocs == 1 && map_calls == 0 && local_maps == 0);
	CHECK(!owner->status.prepare_attempted && !owner->status.program_attempted);
}
static void
retained(void)
{
	unsigned int before = local_reads + local_writes + local_maps + snapshots;
	struct bcmv3d_mmu4_native_status status, saved;
	CHECK(owner->active == NULL && owner->cache == NULL && !owner->executing);
	CHECK(bcmv3d_mmu4_native_cached_status(owner,&status) == 0);
	memcpy(&saved,&status,sizeof(saved));
	hard_irq=true;
	CHECK(bcmv3d_mmu4_native_cached_status(owner,&status) == EWOULDBLOCK);
	CHECK(memcmp(&saved,&status,sizeof(saved)) == 0); hard_irq=false;
	CHECK(local_reads + local_writes + local_maps + snapshots == before);
	CHECK(drm_gets == 1 && drm_puts == 0 && heap_frees == 0 && render_sc.mmu4_native == owner);
}

static void
backing_snapshot_check(void)
{
	struct bcmv3d_mmu4_native_status cached;
	struct bcmv3d_mmu4_backing_status actual;

	CHECK(bcmv3d_mmu4_native_cached_status(owner, &cached) == 0);
	CHECK(bcmv3d_mmu4_backing_status(owner->backing, &actual) == 0);
	CHECK(cached.backing_valid);
	CHECK(cached.backing.prepared == actual.prepared);
	CHECK(cached.backing.sealed == actual.sealed);
	CHECK(cached.backing.first_error == actual.first_error);
	CHECK(cached.backing.prepare_started == actual.prepare_started);
	CHECK(cached.backing.mapping_uncertain == actual.mapping_uncertain);
	CHECK(cached.backing.stage == actual.stage);
	CHECK(cached.backing.frame_bytes == actual.frame_bytes);
	CHECK(cached.backing.control_bytes == actual.control_bytes);
	CHECK(cached.backing.fragments == actual.fragments);
	CHECK(cached.backing.pmap_calls == actual.pmap_calls);
	CHECK(cached.backing.syncs == actual.syncs);
}

static void
scenario(unsigned int which)
{
	struct bcmv3d_mmu4_native *out = &foreign_native;
	unsigned int before;
	int error;

	adapter_setup();
	if (which < 17) {
		switch(which) {
		case 0: hard_irq=true; break;
		case 1: soft_irq=true; break;
		case 2: render_sc.bst=NULL; break;
		case 3: render_sc.dmat=NULL; break;
		case 4: render_device.registered=false; break;
		case 5: render_minor.type=0; break;
		case 6: gpu_node.ad_device=&other_device; break;
		case 7: render_sc.handle=OTHER; break;
		case 8: render_driver.name="other"; break;
		case 9: input.gpu_type=ACPI_TYPE_STRING; break;
		case 10: race_slot=true; break;
		case 11: mutex_enter(&render_sc.memory_lock); break;
		case 12: input.references[0].Type=ACPI_TYPE_STRING; break;
		case 13: namespace_node.ad_device=NULL; break;
		case 14: gpu_device.driver="other"; break;
		case 15: gpu_attachment.ca_name="other"; break;
		case 16: render_device.dev_private=NULL; break;
		}
		before=calls;
		error=bcmv3d_mmu4_native_create(&render_sc,&out); CHECK(error != 0);
		CHECK(out == &foreign_native && local_maps == 0 && map_calls == 0);
		if (which < 2 || which == 11) CHECK(calls == before);
		if (which == 10) CHECK(drm_gets==1 && drm_puts==1 && heap_frees==1);
		return;
	}
	make_owner();
	if(which==17) {
		mutex_enter(&owner->lock);
		CHECK(mmu4_native_backend(owner)==EPROTO);
		mutex_exit(&owner->lock);
		CHECK(bcmv3d_mmu4_native_prepare(owner)==EPROTO);
		CHECK(bcmv3d_mmu4_native_program(owner)==EPROTO);
		CHECK(map_calls==0 && local_maps==0 && snapshots==0 && backing_creates==0);
		retained();return;
	}
	before=calls;
	CHECK(bcmv3d_mmu4_native_create(&render_sc,&out) == EBUSY);
	CHECK(calls==before && drm_gets==1 && heap_allocs==1);
	CHECK(out == &foreign_native);
	if (which >= 20 && which < 35) {
		struct bcm2712_iommuc_observation *o=&storage.observation;
		if (which != 20 && which != 21) CHECK(bcm2712_iommuc_observe_control(&storage)==0);
		switch(which) {
		case 20: race_observation=true; break;
		case 21: snapshot_failure=1; break;
		case 22: o->mapped=false; break;
		case 23: o->raw_valid=false; break;
		case 24: o->reads_attempted=2; break;
		case 25: o->reads_completed=0; break;
		case 26: o->first_error=EIO; break;
		case 27: o->raw_control=8; break;
		case 28: o->raw_control=4; break;
		case 29: storage.mmu4.attempted=true; storage.mmu4.first_error=ERANGE; break;
		case 30: storage.mmu4.attempted=true; break;
		case 31: storage.core.initialized=true; break;
		case 32: o->stage=BCM2712_IOMMUC_OBSERVE_READ; break;
		case 33: o->started=false; break;
		case 34: storage.mmu4.fault=EIO; break;
		}
		error=bcmv3d_mmu4_native_prepare(owner);
		CHECK(error!=0 && local_maps==0 && local_reads==0 && backing_creates==0);
		if(which==20 || which==30) CHECK(error==EALREADY);
		if(which==26) CHECK(error==EIO);
		if(which==29) CHECK(error==ERANGE);
		CHECK(bcmv3d_mmu4_native_prepare(owner)==error);
		retained(); return;
	}
	if(which==35) snapshot_failure=2;
	if(which==36) map_error=ENOMEM;
	if(which==37) control_value=4;
	if(which==38) control_value=8;
	if(which==39) local_map_error=ENOMEM;
	if(which==40) backing_failure=1;
	if(which==41) debug_value=0;
	if(which>=42 && which<=45) { control_value=which-42; CHECK(bcm2712_iommuc_observe_control(&storage)==0); }
	error=bcmv3d_mmu4_native_prepare(owner);
	if(which>=35 && which<=41) {
		CHECK(error!=0 && local_writes==0 && writes==0 && !storage.core.initialized);
		CHECK(bcmv3d_mmu4_native_prepare(owner)==error);retained();return;
	}
	CHECK(error==0 && owner->status.stage==BCMV3D_MMU4_NATIVE_PREPARED);
	CHECK(bcmv3d_mmu4_native_prepare(owner)==EALREADY);
	if(which==46) { storage.mmu4.attempted=true; storage.mmu4.first_error=EAGAIN; }
	if(which==47) snapshot_failure=snapshots+1;
	if(which==48) local_control=1;
	if(which==49) shoot_value=0x80000000;
	if(which==50) debug_value++;
	if(which==51) fail_pages=true;
	if(which==52) { debug_value++;fail_close=true; }
	if(which==53) poll_timeout=true;
	if(which==54) operational_value=8;
	if(which==55) reenter_io=true;
	if(which==56) fail_shared=true;
	if(which>=67 && which<=69) backing_snapshot_failure=3;
	if(which==68) fail_pages=true;
	if(which==69) fail_shared=true;
	if(which==70) snapshot_failure=snapshots+2;
	error=bcmv3d_mmu4_native_program(owner);
	if((which>=46 && which<=54) || which==56 || which>=67) {
		CHECK(error!=0 && owner->status.stage!=BCMV3D_MMU4_NATIVE_COMPLETE);
		if(which<48) CHECK(!owner->status.sealed && local_writes==0);
		else {
			CHECK(owner->status.sealed); native_backing_check(owner->backing);
			if(which<=52) CHECK(local_writes==0);
			if(which>=48 && which<=51) {
				CHECK(storage.core.status.reserved==0);
				CHECK(owner->status.sequence_valid && owner->status.sequence.reservation_owned);
				CHECK(owner->status.sequence.stage!=BCM2712_MMU4_CLOSED);
			}
			if(which==56) {
				CHECK(error==EIO && owner->status.operation_error==EIO);
				CHECK(!owner->status.shared_valid && storage.mmu4.first_error==EIO);
				CHECK(storage.mmu4.fault==EIO && storage.core.status.retained==2);
			}
			if(which==52) CHECK(error==EAGAIN && owner->status.close_error==EBUSY && storage.core.status.retained==2);
		}
		CHECK(bcmv3d_mmu4_native_program(owner)==error);
	} else {
		CHECK(error==0 && local_writes==8 && local_barriers==8 && !pending);
		CHECK(last_offset==0 && last_write==0x04090803);
		CHECK(owner->status.sequence_valid && owner->status.sequence.sequence_complete);
		CHECK(owner->status.sequence.retained && storage.core.status.retained==2);
		CHECK(owner->status.icac.mmu4.attempted && owner->status.icac.core_initialized);
		CHECK(owner->status.shared_valid && owner->status.shared.retained==2);
		CHECK(owner->status.sealed && owner->status.write_attempted);
		CHECK(bcmv3d_mmu4_native_program(owner)==EALREADY);
		native_backing_check(owner->backing);
	}
	if(which<67 || which==70) {
		backing_snapshot_check();
		if(which==51) {
			CHECK(!owner->status.backing.prepared);
			CHECK(owner->status.backing.prepare_started);
			CHECK(owner->status.backing.first_error==EFAULT);
			CHECK(owner->status.backing.mapping_uncertain);
		}
		if(which==53 || which==54 || which==56 || which==70) {
			CHECK(owner->status.backing.prepared);
			CHECK(owner->status.backing.first_error==0);
			CHECK(owner->status.backing.syncs==5);
		}
	} else {
		struct bcmv3d_mmu4_backing_status actual;

		CHECK(bcmv3d_mmu4_backing_status(owner->backing,&actual)==0);
		CHECK(!owner->status.backing_valid && actual.sealed);
		CHECK(actual.prepared==(which!=68));
		CHECK(actual.first_error==(which==68 ? EFAULT : 0));
		CHECK(error==(which==67 ? EAGAIN : which==68 ? EFAULT : EIO));
		CHECK(owner->status.operation_error==(which==67 ? 0 : which==68 ? EFAULT : EIO));
		CHECK(owner->status.status_error==(which==69 ? EIO : EAGAIN));
		CHECK(owner->status.fault==(which==69 ? EIO : EAGAIN));
	}
	if(which>=57 && which<=66) {
		struct bcm2712_mmu4_core local;
		uint32_t value;
		unsigned int accesses=local_reads+local_writes+local_barriers;
		memset(&local,0,sizeof(local));
		local.initialized=true;local.status.retained=true;local.status.mutation_attempted=true;
		/* Direct private-backend misuse controls, not a public callback route. */
		if(which==57) {
			CHECK(mmu4_native_backend(owner)==EDEADLK && owner->status.fault==0);
		}
		mutex_enter(&owner->lock);
		if(which==58) CHECK(mmu4_native_backend(owner)==EPROTO);
		owner->active=&local;owner->cache=&storage.core;owner->executing=true;
		if(which==59) local.status.fault=EIO;
		if(which==60) storage.core.status.fault=ERANGE;
		if(which==61) CHECK(mmu4_native_write(owner,BCM2712_MMU4_DEBUG,2)==EINVAL);
		if(which==62) CHECK(mmu4_native_read(owner,BCM2712_MMU4_CAP,&value)==EINVAL);
		if(which==63) CHECK(mmu4_native_read(owner,BCM2712_MMU4_DEBUG,NULL)==EPROTO);
		if(which==64) { storage.core.status.retained=0;CHECK(mmu4_native_write(owner,BCM2712_MMU4_CONTROL,3)==EPROTO); }
		if(which==66) CHECK(mmu4_native_read(owner,(enum bcm2712_mmu4_register)99,&value)==EINVAL);
		if(which==65) { owner->barrier_pending=true;CHECK(mmu4_native_read(owner,BCM2712_MMU4_DEBUG,&value)==EPROTO); }
		if(which>=58) {
			error=which==59 ? EIO : which==60 ? ERANGE : (which==61 || which==62 || which==66) ? EINVAL : EPROTO;
			CHECK(mmu4_native_read(owner,BCM2712_MMU4_DEBUG,&value)==error);
			CHECK(mmu4_native_write(owner,BCM2712_MMU4_CONTROL,3)==error);
			mmu4_native_barrier(owner);
			CHECK(owner->status.fault==error);
		}
		CHECK(local_reads+local_writes+local_barriers==accesses);
		owner->active=NULL;owner->cache=NULL;owner->executing=false;
		mutex_exit(&owner->lock);
	}
	retained();
}

int
main(void)

{
	unsigned int which, total=0;
	for(which=0;which<71;which++) {
		int fd[2], status;
		pid_t child, waited;
		unsigned int count = 0;
		ssize_t received;
		const char *control = getenv("MMU4_NATIVE_HARNESS_CONTROL");

		if (pipe(fd) != 0) {
			fprintf(stderr,"Native MMU4 harness pipe failure\n");
			return 70;
		}
		child = fork();
		if (child < 0) {
			close(fd[0]);
			close(fd[1]);
			fprintf(stderr,"Native MMU4 harness fork failure\n");
			return 70;
		}
		if (child == 0) {
			close(fd[0]);
			if (which == 0 && control != NULL) {
				if (strcmp(control,"segv") == 0) raise(SIGSEGV);
				if (strcmp(control,"abort") == 0) raise(SIGABRT);
				if (strcmp(control,"check") == 0) CHECK(false);
				if (strcmp(control,"hang") == 0) for (;;) pause();
			}
			scenario(which);
			if (write(fd[1],&checks,sizeof(checks)) != sizeof(checks))
				_exit(70);
			_exit(0);
		}
		close(fd[1]);
		do { received=read(fd[0],&count,sizeof(count)); }
		while (received<0 && errno==EINTR);
		close(fd[0]);
		do { waited=waitpid(child,&status,0); }
		while (waited<0 && errno==EINTR);
		if (waited != child) {
			fprintf(stderr,"Native MMU4 harness wait failure\n");
			return 70;
		}
		if (WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT) {
			/* Preserve the child signal. Do not manufacture a parent CHECK. */
			raise(SIGABRT);
			return 70;
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status)!=0 ||
		    received!=sizeof(count)) {
			fprintf(stderr,"Native MMU4 harness child %u failed: status %#x, bytes %jd\n",
			    which,status,(intmax_t)received);
			return 70;
		}
		total+=count;
	}
	printf("BCMV3D native MMU4 contract: %u checks, 71 isolated boots PASS\n",total+checks);
	return 0;
}
