/* SPDX-License-Identifier: BSD-2-Clause
 * Actual-source ACPI PowerResource diagnosis, AI-assisted.
 * No board, MMIO, native AML or native kernel locking is executed.
 */
#include "queue-native.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef void *ACPI_HANDLE;
typedef uint64_t ACPI_INTEGER;
typedef uint32_t ACPI_STATUS;
typedef pthread_mutex_t kmutex_t;
typedef struct {
	pthread_mutex_t mutex;
	bool done;
} once_t;
#define ONCE_DECL(n) once_t n = {PTHREAD_MUTEX_INITIALIZER, false}
static inline int
run_once(once_t *o, int (*fn)(void))
{
	int e = 0;
	assert(!pthread_mutex_lock(&o->mutex));
	if (!o->done) {
		e = fn();
		if (!e)
			o->done = true;
	}
	assert(!pthread_mutex_unlock(&o->mutex));
	return e;
}
#define RUN_ONCE(o, f) run_once(o, f)
typedef struct acpi_object ACPI_OBJECT;
struct acpi_object {
	unsigned Type;
	struct {
		uint64_t SystemLevel, ResourceOrder;
	} PowerResource;
	struct {
		unsigned Count;
		ACPI_OBJECT *Elements;
	} Package;
	ACPI_HANDLE Reference;
};
typedef struct {
	size_t Length;
	void *Pointer;
} ACPI_BUFFER;
struct acpi_devnode {
	ACPI_HANDLE ad_handle;
};
#define AE_OK 0
#define AE_ERROR 1
#define AE_TYPE 2
#define AE_NOT_FOUND 3
#define AE_BAD_PARAMETER 4
#define AE_LIMIT 5
#define AE_ABORT_METHOD 6
#define AE_CTRL_CONTINUE 7
#define ACPI_TYPE_POWER 1
#define ACPI_TYPE_PACKAGE 2
#define KM_SLEEP 0
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define ACPI_SUCCESS(x) ((x) == AE_OK)
#define ACPI_FAILURE(x) ((x) != AE_OK)
#define ACPI_FREE(p) free(p)
#define ACPI_DEBUG_PRINT(x)                                                    \
	do {                                                                   \
	} while (0)
#define KASSERT(x) assert(x)
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
static void
mutex_init(kmutex_t *m, int a, int b)
{
	(void)a;
	(void)b;
	assert(!pthread_mutex_init(m, NULL));
}
static void
mutex_enter(kmutex_t *m)
{
	assert(!pthread_mutex_lock(m));
}
static void
mutex_exit(kmutex_t *m)
{
	assert(!pthread_mutex_unlock(m));
}
static inline bool
mutex_owned(kmutex_t *m)
{
	int e = pthread_mutex_trylock(m);
	if (!e) {
		assert(!pthread_mutex_unlock(m));
		return false;
	}
	return e == EBUSY;
}
static void *
kmem_zalloc(size_t n, int f)
{
	(void)f;
	void *p = calloc(1, n);
	assert(p);
	return p;
}
static inline void *
kmem_alloc(size_t n, int f)
{
	(void)f;
	void *p = malloc(n);
	assert(p);
	return p;
}
static inline void
kmem_free(void *p, size_t n)
{
	(void)n;
	free(p);
}
static const char *
acpi_xname(ACPI_HANDLE h)
{
	(void)h;
	return "PWR0";
}
static struct acpi_devnode *
acpi_match_node(ACPI_HANDLE h)
{
	static struct acpi_devnode ad;
	ad.ad_handle = h;
	return &ad;
}
static ACPI_STATUS
acpi_eval_struct(ACPI_HANDLE, const char *, ACPI_BUFFER *);
static ACPI_STATUS
AcpiEvaluateObject(ACPI_HANDLE, const char *, void *, void *);
static ACPI_STATUS
acpi_eval_reference_handle(ACPI_OBJECT *o, ACPI_HANDLE *h)
{
	*h = o->Reference;
	return AE_OK;
}
struct acpi_power_res;
ACPI_STATUS
acpi_power_res(ACPI_HANDLE, ACPI_HANDLE, bool);
#include "power-native.inc"

static int resource, resource_two, owner_a, owner_b, owner_c, owner_d, owner_e,
    owner_f;
static unsigned on_calls, off_calls, unlocked_calls;
static bool fail_on, fail_off, package_resource, package_two;
static ACPI_HANDLE fail_on_handle;
static unsigned tests, failures;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static bool race_armed, race_entered, race_release, second_finished;
static bool control_armed, control_entered, control_release;

#define CHECK(x)                                                               \
	do {                                                                   \
		tests++;                                                       \
		if (!(x)) {                                                    \
			failures++;                                            \
			printf("FAIL line%u: %s\n", (unsigned)__LINE__, #x);   \
		}                                                              \
	} while (0)
static ACPI_STATUS
acpi_eval_struct(ACPI_HANDLE h, const char *path, ACPI_BUFFER *b)
{
	ACPI_OBJECT *o;
	(void)h;
	b->Pointer = NULL;
	b->Length = 0;
	if (path == NULL) {
		assert(!pthread_mutex_lock(&gate));
		if (race_armed && !race_entered) {
			race_entered = true;
			assert(!pthread_cond_broadcast(&cv));
			while (!race_release)
				assert(!pthread_cond_wait(&cv, &gate));
		}
		assert(!pthread_mutex_unlock(&gate));
		o = calloc(1, sizeof(*o));
		assert(o);
		o->Type = ACPI_TYPE_POWER;
	} else {
		if (!package_resource)
			return AE_NOT_FOUND;
		o = calloc(3, sizeof(*o));
		assert(o);
		o->Type = ACPI_TYPE_PACKAGE;
		o->Package.Count = package_two ? 2 : 1;
		o->Package.Elements = o + 1;
		o[1].Reference = &resource;
		o[2].Reference = &resource_two;
	}
	b->Pointer = o;
	b->Length = sizeof(*o);
	return AE_OK;
}
static ACPI_STATUS
AcpiEvaluateObject(ACPI_HANDLE h, const char *path, void *a, void *b)
{
	struct acpi_power_res *r;
	(void)a;
	(void)b;
	TAILQ_FOREACH(r, &res_head, res_list)
	{
		if (r->res_handle != h)
			continue;
		int status = pthread_mutex_trylock(&r->res_mutex);
		if (!status) {
			unlocked_calls++;
			assert(!pthread_mutex_unlock(&r->res_mutex));
		}
		break;
	}
	assert(!pthread_mutex_lock(&gate));
	if (control_armed && !control_entered) {
		control_entered = true;
		assert(!pthread_cond_broadcast(&cv));
		while (!control_release)
			assert(!pthread_cond_wait(&cv, &gate));
	}
	assert(!pthread_mutex_unlock(&gate));
	if (!strcmp(path, "_ON")) {
		on_calls++;
		return fail_on && (!fail_on_handle || h == fail_on_handle)
			   ? AE_ERROR
			   : AE_OK;
	}
	if (!strcmp(path, "_OFF")) {
		off_calls++;
		return fail_off ? AE_ERROR : AE_OK;
	}
	abort();
}
static unsigned
owners_for(ACPI_HANDLE handle)
{
	struct acpi_power_res *r;
	unsigned n = 0;
	TAILQ_FOREACH(r, &res_head, res_list)
	if (r->res_handle == handle) for (size_t i = 0;
					  i < __arraycount(r->res_ref);
					  i++) if (r->res_ref[i]) n++;
	return n;
}
static unsigned
owners(void)
{
	return owners_for(&resource);
}
static void
reset(void)
{
	struct acpi_power_res *r, *next;
	TAILQ_FOREACH_SAFE(r, &res_head, res_list, next) {
		assert(!pthread_mutex_destroy(&r->res_mutex));
		free(r);
	}
	TAILQ_INIT(&res_head);
	on_calls = off_calls = unlocked_calls = 0;
	fail_on = fail_off = package_resource = package_two = false;
	fail_on_handle = NULL;
	race_armed = race_entered = race_release = second_finished = false;
	control_armed = control_entered = control_release = false;
}
static void
direct_contract(void)
{
	puts("CASE first/duplicate/shared/last-owner");
	reset();
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	CHECK(on_calls == 1 && off_calls == 0 && owners() == 1);
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	CHECK(on_calls == 1 && owners() == 1);
	CHECK(acpi_power_res(&resource, &owner_b, true) == AE_OK);
	CHECK(on_calls == 1 && owners() == 2);
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_OK);
	CHECK(off_calls == 0 && owners() == 1);
	CHECK(acpi_power_res(&resource, &owner_b, false) == AE_OK);
	CHECK(off_calls == 1 && owners() == 0);
	CHECK(unlocked_calls == 0);
	puts("CASE failed_ON uncertain owner/retry");
	reset();
	fail_on = true;
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_ERROR);
	CHECK(owners() == 1 && on_calls == 1);
	CHECK(acpi_power_res(&resource, &owner_b, true) == AE_ABORT_METHOD);
	CHECK(owners() == 1 && on_calls == 1);
	fail_on = false;
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	CHECK(owners() == 1 && on_calls == 2);
	puts("CASE failed_ON owner can rollback_OFF");
	reset();
	fail_on = true;
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_ERROR);
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_OK);
	CHECK(owners() == 0 && off_calls == 1);
	puts("CASE failed_ON and failed rollback preserve uncertain owner");
	reset();
	fail_on = true;
	fail_off = true;
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_ERROR);
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_ERROR);
	CHECK(owners() == 1 && off_calls == 1);
	fail_off = false;
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_OK);
	CHECK(owners() == 0 && off_calls == 2);
	puts("CASE failed_OFF owner can retry_ON, other owner is blocked");
	reset();
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	fail_off = true;
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_ERROR);
	CHECK(acpi_power_res(&resource, &owner_b, true) == AE_ABORT_METHOD);
	CHECK(owners() == 1 && on_calls == 1);
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	CHECK(owners() == 1 && on_calls == 2);
	puts("CASE failed_OFF retains ownership/retry");
	reset();
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	fail_off = true;
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_ERROR);
	CHECK(owners() == 1 && off_calls == 1);
	fail_off = false;
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_OK);
	CHECK(owners() == 0 && off_calls == 2);
	puts("CASE unknown release/invalid owner/capacity");
	reset();
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_ABORT_METHOD);
	CHECK(off_calls == 0 && on_calls == 0 && owners() == 0);
	CHECK(acpi_power_res(&resource, NULL, true) == AE_BAD_PARAMETER);
	CHECK(on_calls == 0);
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	CHECK(acpi_power_res(&resource, &owner_b, false) == AE_ABORT_METHOD);
	CHECK(off_calls == 0 && owners() == 1);
	CHECK(acpi_power_res(&resource, &owner_b, true) == AE_OK);
	CHECK(acpi_power_res(&resource, &owner_c, true) == AE_OK);
	CHECK(acpi_power_res(&resource, &owner_d, true) == AE_OK);
	CHECK(acpi_power_res(&resource, &owner_e, true) == AE_OK);
	CHECK(acpi_power_res(&resource, &owner_f, true) == AE_LIMIT);
	CHECK(owners() == 5);
	CHECK(on_calls == 1 && off_calls == 0);
	puts("CASE shared-owner deregistration");
	reset();
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	CHECK(acpi_power_res(&resource, &owner_b, true) == AE_OK);
	acpi_power_deregister(&owner_a);
	CHECK(owners() == 1 && off_calls == 0);
	acpi_power_deregister(&owner_b);
	CHECK(owners() == 0 && off_calls == 1);
	puts("CASE failed deregistration retains rollback owner");
	reset();
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	fail_off = true;
	acpi_power_deregister(&owner_a);
	CHECK(owners() == 1 && off_calls == 1);
	fail_off = false;
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_OK);
	CHECK(owners() == 0 && off_calls == 2);
}
static void
switch_contract(void)
{
	struct acpi_devnode ad = {.ad_handle = &owner_a};
	puts("CASE actual switch propagates resource error");
	reset();
	package_resource = true;
	fail_on = true;
	CHECK(acpi_power_switch(&ad, 0, true) == AE_ERROR);
	reset();
	package_resource = true;
	CHECK(acpi_power_res(&resource, &owner_a, true) == AE_OK);
	fail_off = true;
	CHECK(acpi_power_switch(&ad, 3, false) == AE_ERROR);
	puts("CASE switch stops at first resource failure");
	reset();
	package_resource = package_two = fail_on = true;
	CHECK(acpi_power_switch(&ad, 0, true) == AE_ERROR);
	CHECK(on_calls == 1 && owners() == 1 && owners_for(&resource_two) == 0);
	puts("CASE switch preserves prior acquired owners after later failure");
	reset();
	package_resource = package_two = fail_on = true;
	fail_on_handle = &resource_two;
	CHECK(acpi_power_switch(&ad, 0, true) == AE_ERROR);
	CHECK(on_calls == 2 && owners() == 1 && owners_for(&resource_two) == 1);
	CHECK(acpi_power_res(&resource_two, &owner_b, true) == AE_ABORT_METHOD);
	CHECK(acpi_power_res(&resource_two, &owner_a, false) == AE_OK);
	CHECK(acpi_power_res(&resource, &owner_a, false) == AE_OK);
	CHECK(owners() == 0 && owners_for(&resource_two) == 0);
}
struct lookup_arg {
	struct acpi_power_res *result;
	bool second;
};
struct control_arg {
	ACPI_HANDLE owner;
	ACPI_STATUS result;
	bool second;
};
static void *
control_thread(void *p)
{
	struct control_arg *arg = p;
	arg->result = acpi_power_res(&resource, arg->owner, true);
	if (arg->second) {
		assert(!pthread_mutex_lock(&gate));
		second_finished = true;
		assert(!pthread_cond_broadcast(&cv));
		assert(!pthread_mutex_unlock(&gate));
	}
	return NULL;
}
static void
transition_contract(void)
{
	pthread_t thread_a, thread_b;
	struct timespec until;
	struct control_arg a = {.owner = &owner_a},
			   b = {.owner = &owner_b, .second = true};
	puts("CASE whole AML transition serializes competing owners");
	reset();
	CHECK(acpi_power_res_get(&resource) != NULL);
	control_armed = true;
	assert(!pthread_create(&thread_a, NULL, control_thread, &a));
	assert(!pthread_mutex_lock(&gate));
	while (!control_entered)
		assert(!pthread_cond_wait(&cv, &gate));
	assert(!pthread_mutex_unlock(&gate));
	assert(!pthread_create(&thread_b, NULL, control_thread, &b));
	assert(!clock_gettime(CLOCK_REALTIME, &until));
	until.tv_nsec += 200000000;
	if (until.tv_nsec >= 1000000000) {
		until.tv_sec++;
		until.tv_nsec -= 1000000000;
	}
	assert(!pthread_mutex_lock(&gate));
	while (!second_finished) {
		int e = pthread_cond_timedwait(&cv, &gate, &until);
		if (e == ETIMEDOUT)
			break;
		assert(e == 0);
	}
	CHECK(!second_finished);
	control_release = true;
	assert(!pthread_cond_broadcast(&cv));
	assert(!pthread_mutex_unlock(&gate));
	assert(!pthread_join(thread_a, NULL));
	assert(!pthread_join(thread_b, NULL));
	CHECK(a.result == AE_OK && b.result == AE_OK);
	CHECK(owners() == 2 && on_calls == 1);
}
static void *
lookup_thread(void *p)
{
	struct lookup_arg *arg = p;
	arg->result = acpi_power_res_get(&resource);
	if (arg->second) {
		assert(!pthread_mutex_lock(&gate));
		second_finished = true;
		assert(!pthread_cond_broadcast(&cv));
		assert(!pthread_mutex_unlock(&gate));
	}
	return NULL;
}
static void
registry_contract(void)
{
	pthread_t thread_a, thread_b;
	struct acpi_power_res *r;
	unsigned n = 0;
	struct lookup_arg a = {0}, b = {.second = true};
	struct timespec until;
	puts("CASE overlapping first registry lookup (deterministic "
	     "interleaving)");
	reset();
	race_armed = true;
	assert(!pthread_create(&thread_a, NULL, lookup_thread, &a));
	assert(!pthread_mutex_lock(&gate));
	while (!race_entered)
		assert(!pthread_cond_wait(&cv, &gate));
	assert(!pthread_mutex_unlock(&gate));
	assert(!pthread_create(&thread_b, NULL, lookup_thread, &b));
	assert(!clock_gettime(CLOCK_REALTIME, &until));
	until.tv_nsec += 200000000;
	if (until.tv_nsec >= 1000000000) {
		until.tv_sec++;
		until.tv_nsec -= 1000000000;
	}
	assert(!pthread_mutex_lock(&gate));
	while (!second_finished) {
		int e = pthread_cond_timedwait(&cv, &gate, &until);
		if (e == ETIMEDOUT)
			break;
		assert(e == 0);
	}
	race_release = true;
	assert(!pthread_cond_broadcast(&cv));
	assert(!pthread_mutex_unlock(&gate));
	assert(!pthread_join(thread_a, NULL));
	assert(!pthread_join(thread_b, NULL));
	TAILQ_FOREACH(r, &res_head, res_list) n++;
	CHECK(a.result == b.result);
	CHECK(n == 1);
}
int
main(void)
{
	alarm(10);
	direct_contract();
	switch_contract();
	transition_contract();
	registry_contract();
	reset();
	printf(
	    "ACPI power actual-source contract: %u assertions, %u failures\n",
	    tests, failures);
	return failures ? 1 : 0;
}
