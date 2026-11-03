/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native implementation. */
/* Origin: EmberBSD A733 firmware service compatibility adapter. */
#include <sys/param.h>
#include <sys/errno.h>
#include <arm/sunxi/sun60i_a733_gpu_fwservice.h>

int
sun60i_a733_gpu_fwservice_attach(struct pvr_fwservice *s, bus_space_tag_t t,
    bus_space_handle_t h, struct pvr_fwmem *m,
    const struct pvr_fwboot_result *b, const struct pvr_fwprobe_result *p)
{
	if (s == NULL || t == NULL)
		return EINVAL;
	if (s->queue.attached || s->objects != NULL)
		return EBUSY;
	if (m != NULL && ((uintptr_t)m % _Alignof(struct pvr_fwmem) != 0 ||
	    sizeof(*m) - 1 > (uintptr_t)-1 - (uintptr_t)m))
		return EINVAL;
	if (m != NULL && m->profile != PVR_FW_PROFILE_A733)
		return EOPNOTSUPP;
	return pvr_fwservice_attach(s, t, h, m, b, p);
}
int
sun60i_a733_gpu_fwservice_set_events(struct pvr_fwservice *s, void *c,
    int (*p)(void *, uint32_t, struct rogue_fwif_freelist_gs_data *),
    int (*d)(void *, uint32_t, int))
{
	return pvr_fwservice_set_events(s, c, p, d);
}
int
sun60i_a733_gpu_fwservice_alloc(struct pvr_fwservice *s, size_t n,
    struct pvr_fwobject **o)
{
	return pvr_fwservice_alloc(s, n, o);
}
int
sun60i_a733_gpu_fwservice_free(struct pvr_fwservice *s, struct pvr_fwobject **o)
{
	return pvr_fwservice_free(s, o);
}
int
sun60i_a733_gpu_fwservice_flush(struct pvr_fwservice *s)
{
	return pvr_fwservice_flush(s);
}
int
sun60i_a733_gpu_fwservice_command(struct pvr_fwservice *s,
    const struct rogue_fwif_kccb_cmd *c, struct pvr_fwprobe_result *r)
{
	return pvr_fwservice_command(s, c, r);
}
int
sun60i_a733_gpu_fwservice_observe(struct pvr_fwservice *s,
    struct pvr_fwprobe_observation *r)
{
	return pvr_fwservice_observe(s, r);
}
void
sun60i_a733_gpu_fwservice_sync(struct pvr_fwservice *s,
    struct pvr_fwobject *o, int ops)
{
	pvr_fwservice_sync(s, o, ops);
}
