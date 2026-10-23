#include <stdlib.h>
#include <string.h>
#define KM_SLEEP 0
extern size_t hidmt_test_allocations;
static inline void *
kmem_alloc(size_t n, int flags)
{
	void *p = malloc(n);
	(void)flags;
	/* KM_SLEEP does not return NULL in the kernel. */
	if (p == NULL)
		abort();
	hidmt_test_allocations++;
	return p;
}
static inline void *
kmem_zalloc(size_t n, int flags)
{
	void *p = kmem_alloc(n, flags);
	memset(p, 0, n);
	return p;
}
static inline void
kmem_free(void *p, size_t n)
{
	(void)n;
	if (p != NULL) {
		hidmt_test_allocations--;
		free(p);
	}
}
