/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Actual explicit-stage admission checks. */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define SYSCTLFN_ARGS const struct sysctlnode *rnode, const void *newp
#define SYSCTLFN_CALL(node) node, newp
struct sysctlnode {
	void *sysctl_data;
	size_t sysctl_size;
};
struct a733_drm {
	bool lock;
	int stage, fault;
	bool registering, registration_started;
};
static unsigned registrations;
static int registration_error, lookup_error, captured;
static bool acquired, reenter;
static void mutex_enter(bool *lock){
	CHECK(!*lock);
	*lock = true;
}
static void mutex_exit(bool *lock){
	CHECK(*lock);
	*lock = false;
}
static int
sysctl_lookup(SYSCTLFN_ARGS)
{
	CHECK(rnode->sysctl_size == sizeof(int));
	captured = *(int *)rnode->sysctl_data;
	if (!lookup_error && newp)
		*(int *)rnode->sysctl_data = *(const int *)newp;
	return lookup_error;
}
static int a733_stage(SYSCTLFN_ARGS);
static int
a733_register(struct a733_drm *sc)
{
	CHECK(!sc->lock && sc->registering);
	registrations++;
	if (reenter) {
		int one = 1;
		struct sysctlnode node = {&sc->stage, sizeof(int)};
		CHECK(a733_stage(&node, &one) == EBUSY);
		CHECK(registrations == 1);
	}
	sc->registration_started = acquired;
	return registration_error;
}
#include "stage.c"
int
main(void)
{
	for (unsigned scenario = 0; scenario < 14; scenario++) {
		struct a733_drm sc = {0};
		struct sysctlnode stage = {&sc.stage, sizeof(int)}, fault = {&sc.fault, sizeof(int)};
		int desired = 1, expected = 0;
		const void *value = &desired;
		registrations = 0;
		registration_error = lookup_error = 0;
		acquired = reenter = false;
		switch (scenario) {
		case 0:
			value = NULL;
			break;
		case 1:
			desired = 0;
			expected = EINVAL;
			break;
		case 2:
			lookup_error = EFAULT;
			expected = EFAULT;
			break;
		case 3:
			sc.registering = true;
			expected = EBUSY;
			break;
		case 4:
			sc.stage = 1;
			break;
		case 5:
			sc.fault = EIO;
			expected = EIO;
			break;
		case 6:
			registration_error = EAGAIN;
			expected = EAGAIN;
			break;
		case 7:
			registration_error = ENXIO;
			expected = ENXIO;
			break;
		case 8:
			registration_error = ENOMEM;
			expected = ENOMEM;
			break;
		case 9:
			registration_error = EAGAIN;
			expected = EAGAIN;
			acquired = true;
			break;
		case 10:
			registration_error = ENXIO;
			expected = ENXIO;
			acquired = true;
			break;
		case 11:
			reenter = true;
			acquired = true;
			break;
		case 12:
			acquired = true;
			break;
		case 13:
			sc.stage = 1;
			sc.fault = ETIMEDOUT;
			expected = ETIMEDOUT;
			break;
		}
		CHECK(a733_stage(&stage, value) == expected && !sc.lock);
		CHECK(registrations == ((scenario >= 6 && scenario <= 12) ? 1u : 0u));
		if (scenario == 6 || scenario == 7)
			CHECK(sc.fault == 0 && !sc.registering && !sc.stage);
		if (scenario >= 8 && scenario <= 10)
			CHECK(sc.fault == expected && !sc.stage && !sc.registering);
		if (scenario == 11 || scenario == 12)
			CHECK(sc.stage == 1 && !sc.fault && !sc.registering);
		lookup_error = 0;
		CHECK(a733_fault(&fault, NULL) == 0 && captured == sc.fault && !sc.lock);
		if (scenario == 6 || scenario == 7) {
			registration_error = 0;
			acquired = true;
			CHECK(a733_stage(&stage, &desired) == 0 && registrations == 2 && sc.stage == 1);
		}
		if (scenario >= 8 && scenario <= 10) {
			registration_error = 0;
			CHECK(a733_stage(&stage, &desired) == expected && registrations == 1);
		}
	}
	printf("PASS %u explicit registration stage checks\n", checks);
	return 0;
}
