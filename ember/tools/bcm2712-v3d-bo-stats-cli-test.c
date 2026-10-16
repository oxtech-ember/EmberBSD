/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the actual CLI decoder without a kernel or device. */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
static int fixture_sysctl(const char *, void *, size_t *, void *, size_t);
#define sysctlbyname fixture_sysctl
#define main decoder_main
#include "bcm2712-v3d-bo-stats.c"
#undef main
#undef sysctlbyname
static unsigned scenario;
static int
fixture_sysctl(const char *name, void *oldp, size_t *length, void *newp, size_t newlen)
{
	struct bcmv3d_bo_stats *s = oldp;
	assert(strcmp(name, "hw.test.bo_stats") == 0);
	assert(newp == NULL && newlen == 0 && *length == sizeof(*s));
	s->version = BCMV3D_BO_STATS_VERSION;
	s->length = sizeof(*s);
	s->last_stage = BCMV3D_BO_STAGE_DMA_ALLOC;
	s->last_errno = ENOMEM;
	s->last_requested_bytes = UINT64_C(5246976);
	switch (scenario) {
	case 1: s->version++; break;
	case 2: s->length--; break;
	case 3: --*length; break;
	case 4: s->reserved[0] = 1; break;
	case 5: s->reserved[1] = 1; break;
	case 6: s->last_stage = BCMV3D_BO_STAGE_COUNT; break;
	case 7: errno = ENOENT; return -1;
	case 8: errno = ENOMEM; return -1;
	default: break;
	}
	return 0;
}
int
main(void)
{
	char *args[] = {"bo-stats", "hw.test.bo_stats", NULL};
	assert(decoder_main(1, args) == 2);
	assert(decoder_main(2, args) == 0);
	for (scenario = 1; scenario <= 8; scenario++)
		assert(decoder_main(2, args) == 1);
	puts("PASS actual sysctl CLI rejects missing, truncated and incompatible data");
	return 0;
}
