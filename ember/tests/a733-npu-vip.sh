#!/bin/sh
# VIPLite command admission of the production NPU driver.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-npu-vip.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '
/^sun60i_npu_vip_command\(/ {copy=1; print "static int"}
copy {print} copy && /^}/ {copy=0}
' "$src/sys/arch/arm/sunxi/sun60i_a733_npu.c" > "$work/vip.h"
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define A733_NPU_ID_SIZE 0x34
#define VIP_DRV_BUF_MAX 1024
struct sun60i_npu_softc {
	int sc_run_lock;
	bool sc_vip_initialized;
	uint32_t sc_vip_processes;
};
static struct sun60i_npu_softc sc;
#define mutex_enter(m) (*(m) = 1)
#define mutex_exit(m) (*(m) = 0)
#include "vip-structs.h"
/* Register access stubs: the contract exercises admission, not MMIO. */
static int read_reg_calls, write_reg_calls;
static int read_reg_result;
static int sun60i_npu_vip_read_reg(struct sun60i_npu_softc *s,
    struct vip_drv_reg *r) {
	if (r->offset == 0 || r->offset > A733_NPU_ID_SIZE - 4 ||
	    (r->offset & 3) != 0)
		return VIP_STATUS_INVALID;
	read_reg_calls++;
	return read_reg_result;
}
static int sun60i_npu_vip_write_reg(struct sun60i_npu_softc *s,
    const struct vip_drv_reg *r) { write_reg_calls++; return 0; }
#include "vip.h"
int
main(void)
{
	struct vip_drv_init init;
	struct vip_drv_database db;
	struct vip_drv_driver_status st;
	struct vip_drv_reg reg;
	unsigned checks = 0;

	/* INIT: exact version handshake, process accounting. */
	memset(&sc, 0, sizeof(sc));
	init.version = VIP_VERSION; init.video_mem_size = 16 << 20;
	assert(sun60i_npu_vip_command(&sc, 0, &init, sizeof(init)) == 0);
	assert(sc.sc_vip_processes == 1 && sc.sc_vip_initialized);
	assert(sun60i_npu_vip_command(&sc, 0, &init, sizeof(init)) == 0);
	assert(sc.sc_vip_processes == 2);
	init.version = 0x010203;
	assert(sun60i_npu_vip_command(&sc, 0, &init, sizeof(init)) ==
	    VIP_STATUS_NOTSUP);
	assert(sc.sc_vip_processes == 2);
	checks += 3;
	assert(sun60i_npu_vip_command(&sc, 0, &init, 4) ==
	    VIP_STATUS_INVALID);
	checks++;

	/* DESTROY balances the process count. */
	assert(sun60i_npu_vip_command(&sc, 1, NULL, 0) == 0);
	assert(sc.sc_vip_processes == 1 && sc.sc_vip_initialized);
	assert(sun60i_npu_vip_command(&sc, 1, NULL, 0) == 0);
	assert(sc.sc_vip_processes == 0 && !sc.sc_vip_initialized);
	checks += 3;

	/* READ_REG: bounds and alignment, then the stub. */
	memset(&reg, 0, sizeof(reg));
	reg.offset = 0x30;
	assert(sun60i_npu_vip_command(&sc, 2, &reg, sizeof(reg)) == 0);
	assert(read_reg_calls == 1);
	reg.offset = 0x0;
	assert(sun60i_npu_vip_command(&sc, 2, &reg, sizeof(reg)) ==
	    VIP_STATUS_INVALID);
	reg.offset = 0x33;
	assert(sun60i_npu_vip_command(&sc, 2, &reg, sizeof(reg)) ==
	    VIP_STATUS_INVALID);
	reg.offset = 0x1000;
	assert(sun60i_npu_vip_command(&sc, 2, &reg, sizeof(reg)) ==
	    VIP_STATUS_INVALID);
	checks += 4;
	reg.offset = 0x30;
	read_reg_result = VIP_STATUS_IO;
	assert(sun60i_npu_vip_command(&sc, 2, &reg, sizeof(reg)) ==
	    VIP_STATUS_IO);
	read_reg_result = 0;
	checks++;

	/* WRITE_REG reaches its handler; the production body refuses. */
	reg.offset = 0x30;
	assert(sun60i_npu_vip_command(&sc, 3, &reg, sizeof(reg)) == 0);
	assert(write_reg_calls == 1);
	checks += 2;

	/* QUERY_DATABASE: the silicon entry for PID 0x1000003b. */
	memset(&db, 0, sizeof(db));
	assert(sun60i_npu_vip_command(&sc, 10, &db, sizeof(db)) == 0);
	assert(db.core_id == 0x1000003b && db.nn_core_count == 8);
	assert(db.logical_vip_count == 1 && db.vip_sram_size == 512 * 1024);
	checks += 3;
	assert(sun60i_npu_vip_command(&sc, 10, &db, 8) ==
	    VIP_STATUS_INVALID);
	checks++;

	/* QUERY_DRIVER_STATUS reflects the process accounting. */
	memset(&st, 0, sizeof(st));
	assert(sun60i_npu_vip_command(&sc, 21, &st, sizeof(st)) == 0);
	assert(st.init_counter == sc.sc_vip_processes && st.power_state == 0);
	checks += 2;
	assert(sun60i_npu_vip_command(&sc, 0, &(struct vip_drv_init){
	    .version = VIP_VERSION }, sizeof(struct vip_drv_init)) == 0);
	memset(&st, 0, sizeof(st));
	assert(sun60i_npu_vip_command(&sc, 21, &st, sizeof(st)) == 0);
	assert(st.init_counter == 1 && st.power_state == 1);
	checks += 2;

	/* Unsupported commands fail closed. */
	assert(sun60i_npu_vip_command(&sc, 5, NULL, 0) ==
	    VIP_STATUS_NOTSUP);
	assert(sun60i_npu_vip_command(&sc, 29, NULL, 0) ==
	    VIP_STATUS_NOTSUP);
	checks += 2;

	printf("PASS: %u VIPLite admission checks\n", checks);
	return 0;
}
C
# The command body needs the ABI structs; extract them from the driver too.
awk '/^struct vip_drv_envelope \{/,/^\};/' "$src/sys/arch/arm/sunxi/sun60i_a733_npu.c" > "$work/vip-structs.h"
for s in init database driver_status reg; do
	awk "/^struct vip_drv_$s \\{/,/^\\};/" "$src/sys/arch/arm/sunxi/sun60i_a733_npu.c" >> "$work/vip-structs.h"
done
awk '/^#define VIP_/,/^struct vip_drv_envelope/' "$src/sys/arch/arm/sunxi/sun60i_a733_npu.c" | sed '$d' >> "$work/vip-structs.h"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${A733_VIP_CFLAGS:-} -I"$work" "$work/check.c" -o "$work/check"
"$work/check"
