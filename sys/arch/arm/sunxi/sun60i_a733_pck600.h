/* Origin: EmberBSD; fixed GPU/NPU/display domain reservations. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SUN60I_A733_PCK600_H
#define _SUN60I_A733_PCK600_H

int	sun60i_a733_pck_gpu_reserve(int, const void *);
int	sun60i_a733_pck_domain_state(int, u_int,
	    uint32_t *, uint32_t *, uint32_t *);
int	sun60i_a733_pck_gpu_retain(int, const void *);
int	sun60i_a733_pck_gpu_request_on(int, const void *, bool);
int	sun60i_a733_pck_gpu_wait(int, const void *, bool);
int	sun60i_a733_pck_gpu_release(int, const void *);

/* Strict borrowed ON domains: NPU4, display1/10. No power writes. */
int	sun60i_a733_pck_npu_reserve(int, const void *);
int	sun60i_a733_pck_npu_retain(int, const void *);
int	sun60i_a733_pck_npu_release(int, const void *);
int	sun60i_a733_pck_npu_ready(int, const void *);
int	sun60i_a733_pck_display_reserve(int, const void *);
int	sun60i_a733_pck_display_retain(int, const void *);
int	sun60i_a733_pck_display_release(int, const void *);
int	sun60i_a733_pck_display_ready(int, const void *);

struct sun60i_a733_display_power;
/* Complete strict-ON samples under the matching reservation, with no writes. */
int	sun60i_a733_pck_display_snapshot(int, const void *,
	    struct sun60i_a733_display_power *);

#endif
