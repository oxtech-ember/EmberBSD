/* Origin: EmberBSD; AI-assisted actual-source VIPLite smoke controls. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int fixture_open(const char *, int, ...);
static int fixture_ioctl(int, unsigned long, ...);
static int fixture_close(int);
#define open fixture_open
#define ioctl fixture_ioctl
#define close fixture_close
#define main npu_smoke_main
#include "a733-npu-vip-smoke.c"
#undef main
#undef close
#undef ioctl
#undef open
#include "vip-abi.h"

_Static_assert(sizeof(struct vip_env) == sizeof(struct vip_drv_envelope),
    "envelope size");
_Static_assert(offsetof(struct vip_env, buffer) ==
    offsetof(struct vip_drv_envelope, buffer), "envelope buffer offset");
_Static_assert(sizeof(struct vip_init) == sizeof(struct vip_drv_init), "init");
_Static_assert(sizeof(struct vip_db) == sizeof(struct vip_drv_database), "db");
_Static_assert(sizeof(struct vip_reg) == sizeof(struct vip_drv_reg), "register");
_Static_assert(sizeof(struct vip_status) == sizeof(struct vip_drv_driver_status),
    "status");

enum scenario {
	SUCCESS, OPEN_ERROR, INIT_ERROR, INIT_IO_ERROR, DB_ERROR, DB_ID_ERROR,
	DB_NN_ERROR, READ_ERROR, READ_IO_ERROR, READ_PID_ERROR, DESTROY_ERROR,
	CLOSE_ERROR, STATUS_ERROR, STATUS_COUNTER_ERROR, STATUS_POWER_ERROR,
	SCENARIO_COUNT
};
static enum scenario scenario;
static bool software_case;
static unsigned opened, closed, calls, destroys;
static uint32_t commands[5];
static int protocol_error;

static int
fixture_open(const char *path, int flags, ...)
{
	opened++;
	if (strcmp(path, "/fixture/vipcore") != 0 || flags != O_RDWR)
		protocol_error = 1;
	if (scenario == OPEN_ERROR) {
		errno = ENOENT;
		return -1;
	}
	return 23;
}

static int
fixture_close(int fd)
{
	closed++;
	if (fd != 23)
		protocol_error = 1;
	if (scenario == CLOSE_ERROR) {
		errno = EIO;
		return -1;
	}
	return 0;
}

static int
fixture_ioctl(int fd, unsigned long request, ...)
{
	struct vip_env *env;
	va_list ap;

	va_start(ap, request);
	env = va_arg(ap, struct vip_env *);
	va_end(ap);
	if (fd != 23 || request != 30000 || calls >= 5) {
		protocol_error = 1;
		errno = EINVAL;
		return -1;
	}
	commands[calls++] = env->command;
	env->error = 0;
	switch (env->command) {
	case 0: {
		const struct vip_init *init = (const void *)(uintptr_t)env->buffer;
		if (env->bytes != sizeof(*init) || init->version != 0x020000 ||
		    init->video_mem_size != 16U << 20)
			protocol_error = 1;
		if (scenario == INIT_ERROR)
			env->error = -4;
		if (scenario == INIT_IO_ERROR) {
			errno = EIO;
			return -1;
		}
		break;
	}
	case 1:
		destroys++;
		if (env->bytes != 0 || env->buffer != 0)
			protocol_error = 1;
		if (scenario == DESTROY_ERROR)
			env->error = -2;
		break;
	case 10: {
		struct vip_db *db = (void *)(uintptr_t)env->buffer;
		if (env->bytes != sizeof(*db))
			protocol_error = 1;
		memset(db, 0, sizeof(*db));
		db->core_id = scenario == DB_ID_ERROR ? 1 : 0x1000003b;
		db->nn_core_count = scenario == DB_NN_ERROR ? 0 : 8;
		db->vip_sram_size = 512 * 1024;
		if (scenario == DB_ERROR)
			env->error = -2;
		break;
	}
	case 21: {
		struct vip_status *st = (void *)(uintptr_t)env->buffer;
		if (env->bytes != sizeof(*st) || !software_case)
			protocol_error = 1;
		memset(st, 0, sizeof(*st));
		st->init_counter = scenario == STATUS_COUNTER_ERROR ? 0 : 1;
		st->power_state = scenario == STATUS_POWER_ERROR ? 0 : 1;
		if (scenario == STATUS_ERROR)
			env->error = -2;
		break;
	}
	case 2: {
		struct vip_reg *reg = (void *)(uintptr_t)env->buffer;
		if (env->bytes != sizeof(*reg) || reg->offset != 0x30 ||
		    software_case)
			protocol_error = 1;
		reg->value = scenario == READ_PID_ERROR ? 0xdeadbeef : 0x1000003b;
		if (scenario == READ_ERROR)
			env->error = -2;
		if (scenario == READ_IO_ERROR) {
			errno = EIO;
			return -1;
		}
		break;
	}
	default:
		protocol_error = 1;
	}
	return 0;
}

int
main(void)
{
	char *full[] = { "npu-smoke", "/fixture/vipcore", NULL };
	char *software[] = { "npu-smoke", "--software-only", "/fixture/vipcore", NULL };
	unsigned failures = 0, tested = 0;

	for (unsigned mode = 0; mode < 2; mode++) {
		software_case = mode == 1;
		for (scenario = SUCCESS; scenario < SCENARIO_COUNT; scenario++) {
			int result;
			unsigned want_destroy = scenario == OPEN_ERROR ||
			    scenario == INIT_ERROR || scenario == INIT_IO_ERROR ? 0 : 1;
			if ((!software_case && scenario >= STATUS_ERROR) ||
			    (software_case && scenario >= READ_ERROR &&
			    scenario <= READ_PID_ERROR))
				continue;
			opened = closed = calls = destroys = 0;
			protocol_error = 0;
			memset(commands, 0xff, sizeof(commands));
			result = npu_smoke_main(software_case ? 3 : 2,
			    software_case ? software : full);
			if (result != (scenario == SUCCESS ? 0 : 1) || opened != 1 ||
			    closed != (scenario == OPEN_ERROR ? 0U : 1U) ||
			    destroys != want_destroy || protocol_error ||
			    (want_destroy && commands[calls - 1] != 1)) {
				fprintf(stderr, "FAIL: mode %u scenario %d result %d close %u "
				    "destroy %u protocol %d\n", mode, scenario, result, closed,
				    destroys, protocol_error);
				failures++;
			}
			tested++;
		}
	}
	if (failures != 0)
		return 1;
	printf("PASS: %u actual-source smoke scenarios and ABI layouts\n", tested);
	return 0;
}
