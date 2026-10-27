/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD common core linkage without PCI or chip implementations. */
#include "rtw88-test-kernel.h"
#include <dev/ic/rtw88var.h>

int
main(void)
{
	struct rtw88_softc sc = { 0 };
	struct rtw88_caps caps, saved;

	memset(&caps, 0xa5, sizeof(caps));
	memcpy(&saved, &caps, sizeof(saved));
	assert(rtw88_core_attach(&sc) == EINVAL);
	assert(rtw88_get_caps(&sc, &caps) == EINVAL);
	assert(memcmp(&caps, &saved, sizeof(caps)) == 0);
	assert(rtw88_stop(&sc) == EINVAL);
	puts("rtw88 common core: independent link PASS");
	return 0;
}
