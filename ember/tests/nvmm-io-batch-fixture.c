/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct nvmm_machine { int unused; };
struct nvmm_io;
struct nvmm_vcpu { struct { void (*io)(struct nvmm_io *); } cbs; };
typedef uint64_t gvaddr_t;
@IO_DESCRIPTOR@
@CONSTANTS@
static struct nvmm_io *expected_io;
static struct nvmm_machine *expected_mach;
static struct nvmm_vcpu *expected_vcpu;
static uint8_t *batch_start;
static size_t callback_count, read_count, write_count, expected_size;
static int failure;
static void
io_callback(struct nvmm_io *io)
{
	size_t j;

	assert(io == expected_io);
	assert(io->mach == expected_mach && io->vcpu == expected_vcpu);
	if (callback_count == 0)
		batch_start = io->data;
	assert(io->data == batch_start + callback_count * io->size);
	for (j = 0; j < io->size; j++) {
		uint8_t value = (uint8_t)(0x40 + callback_count * io->size + j);
		if (io->in)
			io->data[j] = value;
		else
			assert(io->data[j] == value);
	}
	io->port++;
	callback_count++;
}
static int
read_guest_memory(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu,
    gvaddr_t gva, uint8_t *data, size_t size)
{
	size_t j;

	assert(mach == expected_mach && vcpu == expected_vcpu && gva == 0x2000);
	assert(size == expected_size);
	read_count++;
	if (failure == 1) {
		errno = EFAULT;
		return -1;
	}
	for (j = 0; j < size; j++)
		data[j] = (uint8_t)(0x40 + j);
	return 0;
}
static int
write_guest_memory(struct nvmm_machine *mach, struct nvmm_vcpu *vcpu,
    gvaddr_t gva, uint8_t *data, size_t size)
{
	size_t j;

	assert(mach == expected_mach && vcpu == expected_vcpu && gva == 0x2000);
	assert(size == expected_size);
	write_count++;
	for (j = 0; j < size; j++)
		assert(data[j] == (uint8_t)(0x40 + j));
	if (failure == 2) {
		errno = EIO;
		return -1;
	}
	return 0;
}
@PRODUCTION@
int
main(void)
{
	const struct { size_t size; uint64_t count; size_t done; } cases[] = {
		{1, 0, 0}, {1, 1, 1}, {1, 3, 3}, {1, 32, 32}, {1, 40, 32},
		{2, 3, 3}, {2, 40, 16}, {4, 3, 3}, {4, 40, 8},
		{8, 3, 3}, {8, 40, 4}
	};
	struct nvmm_machine mach = {0};
	struct nvmm_vcpu vcpu = {{io_callback}};
	struct nvmm_io io;
	uint8_t original[8];
	size_t i, j, k, total = 0;
	int result, direction;

	expected_io = &io;
	expected_mach = &mach;
	expected_vcpu = &vcpu;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		for (direction = 0; direction < 2; direction++) {
			for (failure = 0; failure <= 2; failure++) {
				if ((direction && failure == 1) ||
				    (!direction && failure == 2))
					continue;
				memset(original, 0x5a, sizeof(original));
				io = (struct nvmm_io){&mach, &vcpu, 0x100,
				    direction != 0, cases[i].size, original};
				callback_count = read_count = write_count = 0;
				expected_size = cases[i].size * cases[i].done;
				errno = 0;
				result = assist_io_batch(&mach, &vcpu, &io,
				    0x2000, cases[i].count);
				assert(io.data == original);
				assert(result == (failure ? -1 : (int)cases[i].done));
				assert(errno == (failure == 1 ? EFAULT : failure == 2 ? EIO : 0));
				k = failure == 1 ? 0 : cases[i].done;
				assert(callback_count == k);
				assert(io.port == 0x100 + k);
				assert(io.size == cases[i].size && io.in == (direction != 0));
				assert(read_count == (direction ? 0 : 1));
				assert(write_count == (direction ? 1 : 0));
				for (j = 0; j < sizeof(original); j++)
					assert(original[j] == 0x5a);
				io.data[0] = 0xa5;
				assert(original[0] == 0xa5);
				total++;
			}
		}
	}
	assert(total == 44);
	puts("44 batch cases: pointer lifetime, callback identity/data/field changes, count and error paths PASS");
	return 0;
}
