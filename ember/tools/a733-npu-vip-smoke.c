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
struct vip_status {
	uint32_t user_signal_count, user_commit_count, power_state, init_counter;
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
	const char *path;
	struct vip_init init = { .video_mem_size = 16 << 20, .version = 0x020000 };
	struct vip_db db;
	struct vip_reg reg;
	struct vip_status status;
	int fd, error, initialized = 0, result = 1, software_only = 0;

	if (argc > 1 && strcmp(argv[1], "--software-only") == 0) {
		software_only = 1;
		argc--;
		argv++;
	}
	if (argc > 2) {
		fprintf(stderr, "usage: a733-npu-vip-smoke [--software-only] [device]\n");
		return 1;
	}
	path = argc > 1 ? argv[1] : "/dev/vipcore";

	fd = open(path, O_RDWR);
	if (fd < 0) {
		printf("FAIL: open %s: %s\n", path, strerror(errno));
		return 1;
	}
	error = vip_call(fd, 0, &init, sizeof(init));
	if (error != 0) {
		printf("FAIL: INIT error %d\n", error);
		goto out;
	}
	initialized = 1;
	printf("PASS: INIT handshake\n");
	memset(&db, 0, sizeof(db));
	error = vip_call(fd, 10, &db, sizeof(db));
	if (error != 0 || db.core_id != 0x1000003b || db.nn_core_count != 8) {
		printf("FAIL: DATABASE error %d id 0x%x nn %u\n", error,
		    db.core_id, db.nn_core_count);
		goto out;
	}
	printf("PASS: DATABASE id 0x%x nn=%u sram=%u\n",
	    db.core_id, db.nn_core_count, db.vip_sram_size);
	if (software_only) {
		memset(&status, 0, sizeof(status));
		error = vip_call(fd, 21, &status, sizeof(status));
		if (error != 0 || status.init_counter == 0 ||
		    status.power_state != 1) {
			printf("FAIL: STATUS error %d init %u software power %u\n",
			    error, status.init_counter, status.power_state);
			goto out;
		}
		printf("PASS: software initialization status\n");
		result = 0;
		goto out;
	}
	memset(&reg, 0, sizeof(reg));
	reg.offset = 0x30;
	error = vip_call(fd, 2, &reg, sizeof(reg));
	if (error != 0 || reg.value != 0x1000003b) {
		printf("FAIL: READ_REG error %d PID 0x%08x\n", error,
		    reg.value);
		goto out;
	}
	printf("PASS: READ_REG 0x30 = 0x%08x (silicon PID)\n", reg.value);
	result = 0;
out:
	if (initialized) {
		error = vip_call(fd, 1, NULL, 0);
		if (error != 0) {
			printf("FAIL: DESTROY error %d\n", error);
			result = 1;
		}
	}
	if (close(fd) != 0) {
		printf("FAIL: close %s: %s\n", path, strerror(errno));
		result = 1;
	}
	if (result == 0) {
		if (software_only)
			printf("PASS: software ABI; no NPU MMIO or task execution\n");
		else
			printf("PASS: VIPLite handshake and silicon PID; no task execution\n");
	}
	return result;
}
