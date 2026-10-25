/* SPDX-License-Identifier: BSD-2-Clause */
/* Production UAPI and amdgpu_ib_get are inserted by the contract script. */
@LICENSES@
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

@UAPI_FLAG@

_Static_assert(AMDGPU_IB_FLAG_EMIT_MEM_SYNC == (1 << 6),
    "EMIT_MEM_SYNC must be UAPI bit 6");

struct amdgpu_sa_manager {
	unsigned marker;
};

struct amdgpu_sa_bo {
	unsigned marker;
};

struct amdgpu_device {
	struct amdgpu_sa_manager ring_tmp_bo;
	void *dev;
};

struct amdgpu_vm {
	unsigned marker;
};

struct amdgpu_ib {
	struct amdgpu_sa_bo *sa_bo;
	uint64_t gpu_addr;
	uint32_t *ptr;
	uint32_t flags;
};

static unsigned allocator_calls;
static unsigned cpu_addr_calls;
static unsigned gpu_addr_calls;
static unsigned dev_err_calls;
static int allocator_result;
static int last_dev_err;
static unsigned last_size;
static unsigned last_align;
static struct amdgpu_sa_manager *last_manager;
static struct amdgpu_sa_bo *last_accessor_bo;
static struct amdgpu_sa_bo *allocator_bo;
static struct amdgpu_sa_bo allocated_bo = { .marker = 1 };
static uint32_t cpu_words[4];
static const uint64_t expected_gpu_addr = UINT64_C(0x123456789abcdef0);
static int device_marker;

static int
amdgpu_sa_bo_new(struct amdgpu_sa_manager *manager,
    struct amdgpu_sa_bo **sa_bo, unsigned size, unsigned align)
{
	allocator_calls++;
	last_manager = manager;
	last_size = size;
	last_align = align;
	if (allocator_result) {
		*sa_bo = NULL;
		return allocator_result;
	}
	*sa_bo = allocator_bo;
	return 0;
}

static void *
amdgpu_sa_bo_cpu_addr(struct amdgpu_sa_bo *sa_bo)
{
	cpu_addr_calls++;
	last_accessor_bo = sa_bo;
	return cpu_words;
}

static uint64_t
amdgpu_sa_bo_gpu_addr(struct amdgpu_sa_bo *sa_bo)
{
	gpu_addr_calls++;
	last_accessor_bo = sa_bo;
	return expected_gpu_addr;
}

static void
test_dev_err(void *device, const char *format, int error)
{
	(void)format;
	dev_err_calls++;
	if (device != &device_marker)
		last_dev_err = -EINVAL;
	else
		last_dev_err = error;
}

#define dev_err(device, format, ...) \
	test_dev_err((device), (format), __VA_ARGS__)

int amdgpu_ib_get(struct amdgpu_device *adev, struct amdgpu_vm *vm,
	unsigned size, struct amdgpu_ib *ib);

@IB_GET@

#define EXPECT(condition, message) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: %s\n", (message)); \
		return 1; \
	} \
} while (0)

static void
reset_calls(void)
{
	allocator_calls = 0;
	cpu_addr_calls = 0;
	gpu_addr_calls = 0;
	dev_err_calls = 0;
	allocator_result = 0;
	last_dev_err = 0;
	last_size = 0;
	last_align = 0;
	last_manager = NULL;
	last_accessor_bo = NULL;
	allocator_bo = &allocated_bo;
}

static int
test_zero_size(void)
{
	struct amdgpu_device adev = { .dev = &device_marker };
	struct amdgpu_vm vm = { .marker = 2 };
	struct amdgpu_ib ib = {
		.sa_bo = &allocated_bo,
		.gpu_addr = UINT64_C(0x1020304050607080),
		.ptr = cpu_words,
		.flags = 0x20,
	};
	struct amdgpu_ib original = ib;

	reset_calls();
	EXPECT(amdgpu_ib_get(&adev, &vm, 0, &ib) == 0,
	    "size=0: expected success");
	EXPECT(allocator_calls == 0 && cpu_addr_calls == 0 &&
	    gpu_addr_calls == 0 && dev_err_calls == 0,
	    "size=0: unexpected helper call");
	EXPECT(ib.sa_bo == original.sa_bo && ib.gpu_addr == original.gpu_addr &&
	    ib.ptr == original.ptr && ib.flags == original.flags,
	    "size=0: IB fields changed");
	return 0;
}

static int
test_allocator_failure(void)
{
	struct amdgpu_device adev = { .dev = &device_marker };
	struct amdgpu_ib ib = {
		.sa_bo = &allocated_bo,
		.gpu_addr = UINT64_C(0x1020304050607080),
		.ptr = cpu_words,
		.flags = 0x20,
	};
	struct amdgpu_ib original = ib;

	reset_calls();
	allocator_result = -ENOMEM;
	EXPECT(amdgpu_ib_get(&adev, NULL, 128, &ib) == -ENOMEM,
	    "allocator failure: expected error propagation");
	EXPECT(allocator_calls == 1 && last_manager == &adev.ring_tmp_bo &&
	    last_size == 128 && last_align == 256,
	    "allocator failure: unexpected allocation request");
	EXPECT(cpu_addr_calls == 0 && gpu_addr_calls == 0,
	    "allocator failure: address accessor called");
	EXPECT(dev_err_calls == 1 && last_dev_err == -ENOMEM,
	    "allocator failure: expected one error report");
	EXPECT(ib.sa_bo == NULL && ib.gpu_addr == original.gpu_addr &&
	    ib.ptr == original.ptr && ib.flags == original.flags,
	    "allocator failure: output or IB fields mismatch");
	return 0;
}

static int
test_success_without_vm(void)
{
	struct amdgpu_device adev = { .dev = &device_marker };
	struct amdgpu_ib ib = { .flags = 0x20 };

	reset_calls();
	EXPECT(amdgpu_ib_get(&adev, NULL, 128, &ib) == 0,
	    "vm=NULL: expected success");
	EXPECT(ib.sa_bo == &allocated_bo && ib.ptr == cpu_words,
	    "vm=NULL: incorrect SA BO or CPU address");
	EXPECT(ib.gpu_addr == expected_gpu_addr,
	    "vm=NULL: incorrect GPU address");
	EXPECT(ib.flags == AMDGPU_IB_FLAG_EMIT_MEM_SYNC,
	    "vm=NULL: expected EMIT_MEM_SYNC bit");
	EXPECT(allocator_calls == 1 && last_manager == &adev.ring_tmp_bo &&
	    last_size == 128 && last_align == 256,
	    "vm=NULL: unexpected allocation request");
	EXPECT(cpu_addr_calls == 1 && gpu_addr_calls == 1 &&
	    last_accessor_bo == &allocated_bo && dev_err_calls == 0,
	    "vm=NULL: unexpected helper calls");
	return 0;
}

static int
test_success_with_vm(void)
{
	struct amdgpu_device adev = { .dev = &device_marker };
	struct amdgpu_vm vm = { .marker = 3 };
	struct amdgpu_ib ib = {
		.gpu_addr = UINT64_C(0x1020304050607080),
		.flags = 0x20,
	};
	uint64_t original_gpu_addr = ib.gpu_addr;

	reset_calls();
	EXPECT(amdgpu_ib_get(&adev, &vm, 256, &ib) == 0,
	    "vm!=NULL: expected success");
	EXPECT(ib.sa_bo == &allocated_bo && ib.ptr == cpu_words,
	    "vm!=NULL: incorrect SA BO or CPU address");
	EXPECT(ib.gpu_addr == original_gpu_addr,
	    "vm!=NULL: GPU address must remain untouched");
	EXPECT(ib.flags == AMDGPU_IB_FLAG_EMIT_MEM_SYNC,
	    "vm!=NULL: expected EMIT_MEM_SYNC bit");
	EXPECT(allocator_calls == 1 && last_manager == &adev.ring_tmp_bo &&
	    last_size == 256 && last_align == 256,
	    "vm!=NULL: unexpected allocation request");
	EXPECT(cpu_addr_calls == 1 && gpu_addr_calls == 0 &&
	    last_accessor_bo == &allocated_bo && dev_err_calls == 0,
	    "vm!=NULL: unexpected helper calls");
	return 0;
}

int
main(void)
{
	if (test_zero_size() || test_allocator_failure() ||
	    test_success_without_vm() || test_success_with_vm())
		return 1;
	puts("AMDGPU SA-allocated IB execution contract: PASS");
	return 0;
}
