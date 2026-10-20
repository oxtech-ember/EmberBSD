/* SPDX-License-Identifier: BSD-2-Clause */
/* Existing independent publication bus oracle, with actual retained owner wiring. */
#include "publish-oracle.h"
static bool hvs_partial, hvs_bypass;
static unsigned hvs_bypass_mix;
static unsigned hvs_profile_fault, hvs_read_fault, hvs_write_fault;
static unsigned hvs_ignore_write;
static int hvs_query_error;
static uint32_t hvs_cxm;
static bool hvs_timeout, hvs_eof;

static void hvs_before_publish(void)
{
	struct bcmv3d_bootfb_owner *o = &gpu_sc.sc_bootfb;
	struct bcmv3d_display_stop *d = &o->display;
	unsigned variant = (d->stop.version & 0xff) == 0x53 ? 0 : 1;

	publish_setup(variant); pb.regs[0][1] = hvs_cxm;
	pb.timeout = hvs_timeout; pb.eof = hvs_eof;
	/* Transfer all modeled stop words; other profile words are independent literals. */
	pb.regs[0][0] = qb.regs[0][0]; pb.regs[0][8] = qb.regs[0][8];
	for (unsigned i = 0; i < 3; i++) {
		pb.regs[0][(pb.ctrl+i*pb.stride)/4] = qb.regs[0][(qb.ctrl+i*qb.stride)/4];
		pb.regs[0][(pb.stat+i*pb.stride)/4] = qb.regs[0][(qb.stat+i*qb.stride)/4];
	}
	for (unsigned i = 1; i <= 2; i++) { pb.regs[i][0] = qb.regs[i][0]; pb.regs[i][1] = qb.regs[i][1]; }
	if (pb.regs[1][1] == 2) {
		pb.regs[1][0xc / 4] = 0x004a0016;
		pb.regs[1][0x10 / 4] = 0x002c03c0;
	}
	if (hvs_bypass) {
		for (unsigned i = 0; i < __arraycount(bypass_profile); i++) {
			const struct profile_case *r = &bypass_profile[i];

			pb.regs[r->window][r->offset / 4] = r->value;
		}
		if (hvs_bypass_mix != 0) {
			const struct profile_case *r;

			CHECK(hvs_bypass_mix <= 11);
			r = &bypass_profile[hvs_bypass_mix - 1];
			for (unsigned i = 0; i < __arraycount(profile); i++) {
				if (profile[i].window == r->window &&
				    profile[i].offset == r->offset)
					pb.regs[r->window][r->offset / 4] = profile[i].value;
			}
		}
	}
	pb.fail_read = hvs_read_fault; pb.fail_write = hvs_write_fault;
	pb.partial = hvs_partial; pb.ignore_write = hvs_ignore_write;
	if (hvs_profile_fault != 0) {
		const struct profile_case *r = &profile[hvs_profile_fault-1];
		pb.regs[r->window][r->offset/4] ^= r->flip;
	}
	memcpy(pb.saved,pb.regs,sizeof(pb.regs));
	writeback_control[0] |= hvs_busy[0]; writeback_control[1] |= hvs_busy[1];
	bootfb_publish_fault(hvs_native_fault);
	switch (hvs_gate) {
	case 11: o->mmu4.complete = false; break;
	case 12: o->mmu4.fault = EIO; break;
	case 13: o->mmu4.native = NULL; break;
	case 14: o->mmu4.writeback_mapped = 1; break;
	case 15: d->complete = false; break;
	case 16: d->fault = EIO; break;
	case 17: d->mapped = 2; break;
	case 18: d->stop.complete = false; break;
	case 19: d->stop.error = EIO; break;
	case 20: d->stop.pipe[0].complete = false; break;
	case 21: d->stop.pipe[1].complete = false; break;
	case 22: d->stop.pipe[0].error = EIO; break;
	case 23: d->stop.pipe[1].pv_disabled = false; break;
	case 24: d->stop.pipe[1].hvs_disabled = false; break;
	case 25: d->hsm = NULL; break;
	case 26: d->bvb_status.fault = EIO; break;
	case 27: d->core_status.state = 0; break;
	case 28: d->disp_status.leases = 0; break;
	case 29: d->hsm_status.minimum[0] = 0; break;
	case 30: d->bvb_status.rate = 0; break;
	case 31: o->firmware.complete = false; break;
	case 32: o->firmware.fault = EIO; break;
	case 33: o->phase = BCMV3D_BOOTFB_RETAINED; break;
	case 34: o->first_error = EPROTO; break;
	case 35: gpu.private = NULL; break;
	case 36: d->stop.version = 0x55; break;
	case 37: pb.regs[0][0] = 0x55; break;
	case 38: pb.regs[0][0] ^= 7; break;
	case 39: o->renderer = NULL; break;
	case 40: hvs_tuple_error = ENXIO; break;
	}
}
void bootfb_publish_check(const void *n)
{
	CHECK(hvs_permitted && giant == 0 && n == gpu_sc.sc_bootfb.mmu4.native);
	CHECK(gpu_sc.sc_bootfb.publish.attempted && gpu_sc.sc_bootfb.mmu4.complete);
	CHECK(gpu_sc.sc_bootfb.firmware.complete && serial_fixture_held());
	CHECK(hvs_maps == 0 && hvs_writeback_reads == 0 && pb.accesses == 0);
	hvs_native_queries++;if (hvs_callback_case != 0) {
	uint32_t v;
	int error;
	switch (hvs_callback_case) {
	case 1: error = bcmv3d_publish_read(&gpu_sc,(enum bcmv3d_hvs_publish_window)-1,0,&v); break;
	case 2: error = bcmv3d_publish_read(&gpu_sc,BCMV3D_PUBLISH_WINDOWS,0,&v); break;
	case 3: error = bcmv3d_publish_read(&gpu_sc,BCMV3D_PUBLISH_HVS,0,NULL); break;
	case 4: error = bcmv3d_publish_read(&gpu_sc,BCMV3D_PUBLISH_HVS,1,&v); break;
	case 5: error = bcmv3d_publish_read(&gpu_sc,BCMV3D_PUBLISH_HVS,0x1a000,&v); break;
	case 6: error = bcmv3d_publish_write(&gpu_sc,BCMV3D_PUBLISH_HDMI0,0,0); break;
	default: CHECK(false); return;
	}
	CHECK(error == (hvs_callback_case == 6 ? EACCES : EINVAL));
}

}
static unsigned hvs_window(bus_space_handle_t h)
{
	if ((uintptr_t)h <= 3) { CHECK((uintptr_t)h >= 1); return (unsigned)(uintptr_t)h-1; }
	CHECK((uintptr_t)h >= 0x103 && (uintptr_t)h <= 0x108);
	return (unsigned)(uintptr_t)h-0x100;
}
static void hvs_reentry(void)
{
	CHECK(hvs_permitted && giant == 0 && hvs_native_queries == 1 && hvs_callback_case == 0);
	CHECK(gpu_sc.sc_bootfb.publish.attempted && gpu_sc.sc_bootfb.publish.status.native_valid);
	CHECK(gpu_sc.sc_bootfb.publish.status.native.backing.prepared);
	CHECK(gpu_sc.sc_bootfb.publish.status.native.sequence.sequence_complete);
	if (hvs_reenter) {
		unsigned before = hvs_maps+hvs_writeback_reads+pb.accesses;
		CHECK(bcmv3d_bootfb_publish(&gpu_sc) == EBUSY && giant == 0);
		CHECK(hvs_maps+hvs_writeback_reads+pb.accesses == before);
	}
}
static int hvs_bus_map(bus_space_tag_t t,uint64_t a,uint64_t z,int f,bus_space_handle_t *out)
{
	static const unsigned index[] = {3,5,6,11,16,17,18,19,22};
	hvs_reentry(); CHECK(t == &tag && f == 0 && out != NULL);
	CHECK(hvs_writeback_reads == 2);
	for (unsigned i = 3; i < 9; i++) {
		if (a != bcm2712_display_resources[index[i]].memory.address) continue;
		CHECK(i == (unsigned)gpu_sc.sc_bootfb.publish.status.window);
		CHECK(z == bcm2712_display_resources[index[i]].memory.length);
		hvs_maps++; if (hvs_maps == hvs_fail_map) return ENOMEM;
		*out = (bus_space_handle_t)(uintptr_t)(0x100+i); return 0;
	}
	CHECK(false); return EINVAL;
}
static int hvs_bus_read(bus_space_tag_t t,bus_space_handle_t h,uint32_t o,uint32_t *v)
{
	hvs_reentry(); CHECK(t == &tag && v != NULL);
	if ((uintptr_t)h == 4 || (uintptr_t)h == 5) {
		unsigned i = (unsigned)(uintptr_t)h-4;
		CHECK(o == 0x0c && hvs_writeback_reads == i && hvs_maps == 0 && pb.accesses == 0);
		hvs_writeback_reads++; if (hvs_writeback_reads == hvs_fail_writeback) return EIO;
		*v = writeback_control[i]; return 0;
	}
	if (hvs_query_error != 0 && gpu_sc.sc_bootfb.publish.status.complete) return hvs_query_error;
	return publish_test_read(&pb,(enum bcmv3d_hvs_publish_window)hvs_window(h),o,v);
}
static int hvs_bus_write(bus_space_tag_t t,bus_space_handle_t h,uint32_t o,uint32_t v)
{ hvs_reentry(); CHECK(t == &tag); return publish_test_write(&pb,(enum bcmv3d_hvs_publish_window)hvs_window(h),o,v); }
static void hvs_bus_barrier(bus_space_tag_t t,bus_space_handle_t h,unsigned o,uint64_t z,int f)
{
	unsigned w = hvs_window(h); hvs_reentry();
	CHECK(t == &tag && o == 0 && z == (w == 0 ? 0x1a000U : 0x100U) && f == 3);
	publish_test_barrier(&pb,(enum bcmv3d_hvs_publish_window)w);
}
static void hvs_bus_delay(unsigned us)
{
	hvs_reentry();
	if (us == 20000) { CHECK(gpu_sc.sc_bootfb.publish.status.complete); hvs_progress_delayed = !hvs_progress_stuck; }
	else publish_test_delay(&pb,us);
}
