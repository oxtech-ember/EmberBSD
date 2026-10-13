/* Origin: EmberBSD; userspace VIPLite handshake smoke test for A733 NPU. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define VIP_DRV_IOCTL 30000

struct vip_env {
	uint32_t command;
	int32_t error;
	uint32_t bytes;
	uint64_t buffer;
};
struct vip_init {
	uint32_t video_mem_size;
	uint32_t version;
};
struct vip_db {
	uint32_t logical_vip_count, core_count, core_id, nn_core_count;
	uint32_t tp_core_count, vip_sram_size, axi_sram_size, axi_bus_width;
};
struct vip_reg {
	uint64_t address_unused;
	uint32_t offset;
	uint32_t value;
};

static int
vip_call(int fd, uint32_t command, void *data, uint32_t bytes)
{
	struct vip_env env = { .command = command, .bytes = bytes,
	    .buffer = (uint64_t)(uintptr_t)data };

	if (ioctl(fd, VIP_DRV_IOCTL, &env) != 0)
		return -errno;
	return env.error;
}

int
main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/vipcore";
	struct vip_init init = { .video_mem_size = 16 << 20, .version = 0x020000 };
	struct vip_db db;
	struct vip_reg reg;
	int fd, error;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("FAIL: open %s: %s\n", path, strerror(errno));
		return 1;
	}
	error = vip_call(fd, 0, &init, sizeof(init));
	if (error != 0) {
		printf("FAIL: INIT error %d\n", error);
		return 1;
	}
	printf("PASS: INIT handshake\n");
	memset(&db, 0, sizeof(db));
	error = vip_call(fd, 10, &db, sizeof(db));
	if (error != 0 || db.core_id != 0x1000003b || db.nn_core_count != 8) {
		printf("FAIL: DATABASE error %d id 0x%x nn %u\n", error,
		    db.core_id, db.nn_core_count);
		return 1;
	}
	printf("PASS: DATABASE id 0x%x rev-stage nn=%u sram=%u\n",
	    db.core_id, db.nn_core_count, db.vip_sram_size);
	memset(&reg, 0, sizeof(reg));
	reg.offset = 0x30;
	error = vip_call(fd, 2, &reg, sizeof(reg));
	if (error != 0) {
		printf("FAIL: READ_REG error %d\n", error);
		return 1;
	}
	printf("PASS: READ_REG 0x30 = 0x%08x%s\n", reg.value,
	    reg.value == 0x1000003b ? " (silicon PID)" : "");
	error = vip_call(fd, 1, NULL, 0);
	if (error != 0) {
		printf("FAIL: DESTROY error %d\n", error);
		return 1;
	}
	close(fd);
	printf("PASS: two handshake commands, database and register read\n");
	return 0;
}
