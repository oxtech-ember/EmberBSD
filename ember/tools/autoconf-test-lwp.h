/* SPDX-License-Identifier: BSD-2-Clause */
/* Opaque, per-thread kernel identity for actual-source autoconf fixtures. */
#ifndef EMBER_AUTOCONF_TEST_LWP_H
#define EMBER_AUTOCONF_TEST_LWP_H

typedef struct autoconf_test_lwp { int unused; } lwp_t;

static inline lwp_t *
autoconf_test_curlwp(void)
{
	static _Thread_local lwp_t identity;

	return &identity;
}

#define curlwp (autoconf_test_curlwp())

#endif /* EMBER_AUTOCONF_TEST_LWP_H */
