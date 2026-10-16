/* SPDX-License-Identifier: BSD-2-Clause */
/* Read one versioned native-endian V3D BO snapshot. This tool never writes it. */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "../../sys/external/bsd/drm2/bcmv3d/bcmv3d_bo_stats.h"

_Static_assert(sizeof(struct bcmv3d_bo_stats) == 192, "snapshot v1 layout");

int
main(int argc, char **argv)
{
	static const char *const stages[BCMV3D_BO_STAGE_COUNT] = {
		"none", "request", "quota", "va", "dma_alloc", "dma_map",
		"map_create", "map_load", "geometry", "handle"
	};
	struct bcmv3d_bo_stats s;
	size_t length = sizeof(s);
	unsigned i;

	if (argc != 2) {
		fprintf(stderr, "usage: %s hw.<device>.bo_stats\n", argv[0]);
		return 2;
	}
	memset(&s, 0, sizeof(s));
	if (sysctlbyname(argv[1], &s, &length, NULL, 0) == -1) {
		perror(argv[1]);
		return 1;
	}
	if (length != sizeof(s) || s.version != BCMV3D_BO_STATS_VERSION ||
	    s.length != sizeof(s) || s.last_stage >= BCMV3D_BO_STAGE_COUNT ||
	    s.reserved[0] != 0 || s.reserved[1] != 0) {
		fprintf(stderr, "unsupported or malformed BO snapshot\n");
		return 1;
	}
	printf("version=%u length=%u\n", s.version, s.length);
	printf("reserved_bytes=%" PRIu64 " highwater_bytes=%" PRIu64
	    " limit_bytes=%" PRIu64 " max_bo_bytes=%" PRIu64 "\n",
	    s.reserved_bytes, s.highwater_bytes, s.limit_bytes, s.max_bo_bytes);
	printf("reservations=%" PRIu64 " releases=%" PRIu64
	    " failures=%" PRIu64 "\n", s.reservations, s.releases, s.failures);
	for (i = 0; i < BCMV3D_BO_STAGE_COUNT; i++)
		printf("failures_%s=%" PRIu64 "\n", stages[i], s.failures_by_stage[i]);
	printf("last_stage=%s last_errno=%u last_requested_bytes=%" PRIu64
	    " last_rounded_bytes=%" PRIu64 " last_reserved_bytes=%" PRIu64 "\n",
	    stages[s.last_stage], s.last_errno, s.last_requested_bytes,
	    s.last_rounded_bytes, s.last_reserved_bytes);
	return 0;
}
