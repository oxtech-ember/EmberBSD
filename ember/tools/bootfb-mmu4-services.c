/* SPDX-License-Identifier: BSD-2-Clause */
/* Whole actual native/provider/executor bodies and existing controlled services. */
#define bcm2712_display_resources reference_display_resources
#define bcm2712_display_acpi_resources reference_display_acpi_resources
#define bcm2712_display_acpi_observe reference_display_acpi_observe
#include "native-reference.h"
#undef bcm2712_display_resources
#undef bcm2712_display_acpi_resources
#undef bcm2712_display_acpi_observe

void bootfb_backing_reboot(void);
void bootfb_mmu4_check(unsigned, const void *);
static unsigned boot_calls;
static int boot_failure;

void *bootfb_mmu4_reboot(void)
{
	if (owner != NULL) {
		CHECK(!owner->executing && owner->active == NULL && owner->cache == NULL);
		CHECK(!owner->lock.held && !bcm2712_iommuc_mutex.held);
		free(owner); owner = NULL;
	}
	bootfb_backing_reboot();
	drm_gets = drm_puts = heap_allocs = heap_frees = 0;
	local_maps = local_reads = local_writes = local_barriers = 0;
	snapshots = observes = backing_creates = snapshot_failure = backing_failure = 0;
	backing_snapshots = backing_snapshot_failure = 0; local_map_error = 0;
	race_observation = fail_pages = fail_close = race_slot = reenter_io = fail_shared = false;
	local_control = shoot_value = last_write = 0; last_offset = 0; pending = false;
	boot_calls = 0; boot_failure = 0;
	adapter_setup();
	return &render_sc;
}
void bootfb_mmu4_failure(int value)
{
	boot_failure = value;
	/* Faults within actual APIs exercise their retained native status paths. */
	if (value == 5) local_map_error = ENOMEM;
	if (value == 6) fail_pages = true;
	if (value == 7) local_control = 1;
	if (value == 8) debug_value = 0;
	if (value == 9) shoot_value = 0x80000000U;
	if (value == 10) fail_shared = true;
}
unsigned bootfb_mmu4_calls(void) { return boot_calls; }
int bootfb_native_create(struct bcmv3d_device *sc, struct bcmv3d_mmu4_native **out)
{
	bootfb_mmu4_check(1,sc); boot_calls++;
	if (boot_failure == 1) return ENOMEM;
	int error = bcmv3d_mmu4_native_create(sc,out);
	if (error == 0) owner = *out;
	return error;
}
int bootfb_native_prepare(struct bcmv3d_mmu4_native *n)
{
	bootfb_mmu4_check(2,n); boot_calls++;
	if (boot_failure == 2) return EAGAIN;
	return bcmv3d_mmu4_native_prepare(n);
}
int bootfb_native_program(struct bcmv3d_mmu4_native *n)
{
	bootfb_mmu4_check(3,n); boot_calls++;
	if (boot_failure == 3 || boot_failure == 23) return EIO;
	return bcmv3d_mmu4_native_program(n);
}
int bootfb_native_status(struct bcmv3d_mmu4_native *n, struct bcmv3d_mmu4_native_status *s)
{
	bootfb_mmu4_check(4,n); boot_calls++;
	if (boot_failure == 4 || boot_failure == 23) return EFAULT;
	int error = bcmv3d_mmu4_native_cached_status(n,s);
	if (error == 0 && boot_failure >= 11) {
		switch (boot_failure) {
		case 11: s->backing.prepared = false; break;
		case 12: s->backing.sealed = false; break;
		case 13: s->sequence.sequence_complete = false; break;
		case 14: s->sequence.retained = false; break;
		case 15: s->shared.flush_complete = false; break;
		case 16: s->shared.retained = 0; break;
		case 17: s->icac_valid = false; break;
		case 18: s->fault = EPROTO; break;
		case 19: s->stage = BCMV3D_MMU4_NATIVE_PROGRAM; break;
		case 20: s->backing_valid = false; break;
		case 21: s->sequence_valid = false; break;
		case 22: s->shared_valid = false; break;
		}
	}
	return error;
}
