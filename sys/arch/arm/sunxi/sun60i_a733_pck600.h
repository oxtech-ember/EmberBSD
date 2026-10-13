/* Origin: EmberBSD; bounded, read-only GPU domain reservation. */
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

#endif
