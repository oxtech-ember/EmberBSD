/* SPDX-License-Identifier: BSD-2-Clause
 * Origin: EmberBSD; controlled I/O peers for the actual a.out nlist function.
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

struct nlist {
	union { char *n_name; long n_strx; } n_un;
	unsigned long n_value;
	int n_type, n_other, n_desc;
};
struct exec { size_t a_syms; };
struct fixture_stat { off_t st_size; };
static const char strings[] = "\0found\0";
static unsigned allocations, releases, stack_allocations, unmaps;
static int read_error;
static void *owned;

static ssize_t
fixture_pread(int fd, void *data, size_t size, off_t offset)
{
	assert(fd == 3 && offset == 0 && size == sizeof(struct exec));
	((struct exec *)data)->a_syms = sizeof(struct nlist);
	return (ssize_t)size;
}

static int
fixture_fstat(int fd, struct fixture_stat *st)
{
	assert(fd == 3);
	st->st_size = (off_t)(sizeof(struct nlist) + sizeof(strings));
	return 0;
}

static void *
fixture_mmap(void *address, size_t size, int prot, int flags, int fd,
    off_t offset)
{
	assert(address == NULL && size == sizeof(strings));
	assert(prot == 1 && flags == 6 && fd == 3);
	assert(offset == (off_t)sizeof(struct nlist));
	return (void *)strings;
}

static int
fixture_munmap(void *address, size_t size)
{
	assert(address == strings && size == sizeof(strings));
	++unmaps;
	return 0;
}

static off_t
fixture_lseek(int fd, off_t offset, int whence)
{
	assert(fd == 3 && offset == 0 && whence == SEEK_SET);
	return offset;
}

static ssize_t
fixture_read(int fd, void *data, size_t size)
{
	struct nlist symbol = { .n_un.n_strx = 1, .n_value = 42,
	    .n_type = 2, .n_other = 3, .n_desc = 4 };
	assert(fd == 3 && size == sizeof(symbol));
	if (read_error)
		return -1;
	memcpy(data, &symbol, sizeof(symbol));
	return (ssize_t)size;
}

static void *
fixture_malloc(size_t size)
{
	assert(size == 2 && owned == NULL);
	++allocations;
	owned = malloc(size);
	assert(owned != NULL);
	return owned;
}

static void
fixture_free(void *memory)
{
	assert(memory == owned && owned != NULL);
	++releases;
	free(memory);
	owned = NULL;
}

#define stat fixture_stat
#define pread fixture_pread
#define fstat fixture_fstat
#define mmap fixture_mmap
#define munmap fixture_munmap
#define lseek fixture_lseek
#define read fixture_read
#define malloc fixture_malloc
#define free fixture_free
#undef alloca
#define alloca(size) (++stack_allocations, __builtin_alloca(size))
#define _DIAGASSERT assert
#define N_BADMAG(ex) 0
#define N_SYMOFF(ex) 0
#define SIZE_T_MAX SIZE_MAX
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define PROT_READ 1
#define MAP_PRIVATE 2
#define MAP_FILE 4
#define N_STAB 0xe0
#define ISLAST(p) ((p)->n_un.n_name == NULL)

/* @ACTUAL_FUNCTION@ */

int
main(void)
{
	unsigned i;
	for (i = 0; i != 2; ++i) {
		struct nlist query[3] = {
		    { .n_un.n_name = "found", .n_value = 99 },
		    { .n_un.n_name = "missing", .n_value = 99 },
		    { .n_un.n_name = NULL } };
		read_error = (int)i;
		assert(__fdnlist_aout(3, query) == (read_error ? 2 : 1));
		assert(query[0].n_value == (read_error ? 0U : 42U));
		assert(query[1].n_value == 0 && query[1].n_type == 0);
		if (owned != NULL || unmaps != i + 1)
			return 1;
	}
#if defined(__SSP__) || defined(__SSP_ALL__) || defined(__SSP_STRONG__)
	assert(allocations == 2 && releases == 2 && stack_allocations == 0);
#else
	assert(allocations == 0 && releases == 0 && stack_allocations == 2);
#endif
	puts("PASS actual nlist allocation, release and lookup");
	return 0;
}
