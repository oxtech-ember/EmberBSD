/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Run the real passive cooling step, processor limit broker and CPPC
 * limit with controlled ACPI providers. AI-assisted.
 */
#define _DEFAULT_SOURCE
#define _NETBSD_SOURCE
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static unsigned int checks;

/* Kernel services used by the extracted sources. */
#define MAXCPUS 4
#define KM_SLEEP 0
#define IPL_NONE 0
#define MUTEX_DEFAULT 0
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef howmany
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#endif
#define atomic_load_relaxed(p) (*(p))
#define atomic_store_relaxed(p, v) (*(p) = (v))
#define ONCE_DECL(o) int o
#define RUN_ONCE(o, f) (*(o) ? 0 : (*(o) = 1, (f)()))

typedef int kmutex_t;
struct cpu_info {
	unsigned int ci_index;
};

static unsigned int normal_messages, error_messages;
static int lock_depth, allocations;
static int hz = 100;

static void mutex_init(kmutex_t *m, int t, int i) { (void)t; (void)i; *m = 0; }
static void mutex_enter(kmutex_t *m) { CHECK(*m == 0); *m = 1; lock_depth++; }
static void mutex_exit(kmutex_t *m) { CHECK(*m == 1); *m = 0; lock_depth--; }
static unsigned int cpu_index(struct cpu_info *ci) { return ci->ci_index; }

static void *
kmem_zalloc(size_t size, int flags)
{
	void *p = calloc(1, size);

	(void)flags;
	CHECK(p != NULL);
	allocations++;
	return p;
}

static void
kmem_free(void *p, size_t size)
{
	(void)size;
	allocations--;
	free(p);
}

#include <dev/acpi/acpi_perflimit.h>
#include "perflimit-body.h"

/* A recording provider for the broker contract. */
struct provider {
	unsigned int calls, limit;
	int fail;
};

static int
provider_apply(void *arg, u_int limit)
{
	struct provider *p = arg;

	CHECK(lock_depth > 0);
	p->calls++;
	if (p->fail != 0)
		return p->fail;
	p->limit = limit;
	return 0;
}

/* Thermal zone types and the extracted passive cooling functions. */
#define ACPI_TYPE_PACKAGE 1
#define SYSCTLFN_ARGS const struct sysctlnode *rnode, const void *newp
#define SYSCTLFN_PROTO const struct sysctlnode *, const void *
#define SYSCTLFN_CALL(n) (n), newp

typedef void *ACPI_HANDLE;
typedef int ACPI_STATUS;
typedef uint64_t ACPI_INTEGER;
typedef struct { void *Pointer; } ACPI_BUFFER;
typedef struct { int state; uint64_t value_cur; } envsys_data_t;
typedef struct acpi_object ACPI_OBJECT;
struct acpi_object { int Type; };
struct callout { int unused; };
struct acpi_devnode;
struct sysmon_envsys;
struct sysctllog;
struct sysctlnode {
	int sysctl_num;
	void *sysctl_data;
};

static int read_value;

static int
sysctl_lookup(SYSCTLFN_ARGS)
{
	if (newp != NULL)
		*(u_int *)rnode->sysctl_data = *(const u_int *)newp;
	else
		read_value = (int)*(u_int *)rnode->sysctl_data;
	return 0;
}

#include "acpitz-types.h"

typedef struct acpitz_softc *device_t;

static struct acpitz_softc *device_private(device_t dv) { return dv; }

/* Deferred ACPI work and the poll callout, in call order. */
#define OSL_NOTIFY_HANDLER 0
static void (*queued)(void *);
static int scheduled, schedules;
static char order[16];

static ACPI_STATUS
AcpiOsExecute(int type, void (*func)(void *), void *arg)
{
	(void)type;
	(void)arg;
	queued = func;
	strncat(order, "q", sizeof(order) - strlen(order) - 1);
	return 0;
}

static void
callout_schedule(struct callout *c, int ticks)
{
	(void)c;
	scheduled = ticks;
	schedules++;
	strncat(order, "s", sizeof(order) - strlen(order) - 1);
}

static void acpitz_get_status(void *);
static void acpitz_reschedule(struct acpitz_softc *);
static const char *acpitz_celcius_string(uint32_t t) { (void)t; return "t"; }

static void
aprint_normal_dev(device_t dv, const char *fmt, ...)
{
	(void)dv;
	(void)fmt;
	normal_messages++;
}

static void
aprint_error_dev(device_t dv, const char *fmt, ...)
{
	(void)dv;
	(void)fmt;
	error_messages++;
}

#include "acpitz-passive.h"

/* CPPC types, a simulated desired-performance register and its functions. */
#define ACPI_FAILURE(rv) ((rv) != 0)
#define AE_OK 0
#define AE_ERROR 1

#include "cppc-types.h"

static ACPI_INTEGER desired;
static unsigned int writes;
static bool failed_write;

static ACPI_STATUS
cppc_read(struct cppc_softc *sc, CPCPackageElement index, ACPI_INTEGER *val)
{
	(void)sc;
	CHECK(index == CPCDesiredPerformanceReg);
	*val = desired;
	return AE_OK;
}

static ACPI_STATUS
cppc_write(struct cppc_softc *sc, CPCPackageElement index, ACPI_INTEGER val)
{
	CHECK(index == CPCDesiredPerformanceReg);
	CHECK(sc->sc_lock == 1);
	writes++;
	if (failed_write)
		return AE_ERROR;
	desired = val;
	return AE_OK;
}

static ACPI_INTEGER cppc_limited_perf(const struct cppc_softc *);

#include "cppc-functions.h"

static struct cpu_info cpus[MAXCPUS] = { {0}, {1}, {2}, {3} };

static void
broker_contract(void)
{
	struct provider p0 = { 0, ACPI_PERFLIMIT_NONE, 0 };
	int owners[ACPI_PERFLIMIT_OWNERS + 1] = { 0 }, a = 0, b = 0;
	unsigned int i;

	CHECK(acpi_perflimit_set(&cpus[0], &a, 50) == ENODEV);
	CHECK(acpi_perflimit_applied(&cpus[0]) == ACPI_PERFLIMIT_NONE);
	CHECK(acpi_perflimit_attach(&cpus[0], provider_apply, &p0) == 0);
	CHECK(acpi_perflimit_attach(&cpus[0], provider_apply, &p0) == EEXIST);
	CHECK(allocations == 1);
	CHECK(acpi_perflimit_set(&cpus[0], NULL, 50) == EINVAL);

	/* The strictest request is applied; others wait behind it. */
	CHECK(acpi_perflimit_set(&cpus[0], &a, 70) == 0 && p0.limit == 70);
	CHECK(acpi_perflimit_set(&cpus[0], &b, 50) == 0 && p0.limit == 50);
	CHECK(acpi_perflimit_set(&cpus[0], &b, 90) == 0 && p0.limit == 70);
	CHECK(p0.calls == 3);
	CHECK(acpi_perflimit_set(&cpus[0], &b, 80) == 0 && p0.calls == 3);
	CHECK(acpi_perflimit_set(&cpus[0], &a, ACPI_PERFLIMIT_NONE) == 0);
	CHECK(p0.limit == 80 && acpi_perflimit_applied(&cpus[0]) == 80);
	CHECK(acpi_perflimit_set(&cpus[0], &b, 1000) == 0);
	CHECK(p0.limit == ACPI_PERFLIMIT_NONE);

	/* A failed apply keeps the request and retries it. */
	p0.fail = EIO;
	CHECK(acpi_perflimit_set(&cpus[0], &a, 30) == EIO);
	CHECK(acpi_perflimit_applied(&cpus[0]) == ACPI_PERFLIMIT_NONE);
	p0.fail = 0;
	CHECK(acpi_perflimit_set(&cpus[0], &a, 30) == 0 && p0.limit == 30);
	CHECK(acpi_perflimit_set(&cpus[0], &a, ACPI_PERFLIMIT_NONE) == 0);

	/* Owner slots are bounded; withdrawing frees one. */
	for (i = 0; i < ACPI_PERFLIMIT_OWNERS; i++)
		CHECK(acpi_perflimit_set(&cpus[0], &owners[i], 90 - i) == 0);
	CHECK(acpi_perflimit_set(&cpus[0], &owners[i], 10) == ENOSPC);
	CHECK(p0.limit == 90 - ACPI_PERFLIMIT_OWNERS + 1);
	CHECK(acpi_perflimit_set(&cpus[0], &owners[0], ACPI_PERFLIMIT_NONE) == 0);
	CHECK(acpi_perflimit_set(&cpus[0], &owners[i], 10) == 0 && p0.limit == 10);
	for (i = 0; i <= ACPI_PERFLIMIT_OWNERS; i++)
		CHECK(acpi_perflimit_set(&cpus[0], &owners[i],
		    ACPI_PERFLIMIT_NONE) == 0);
	CHECK(p0.limit == ACPI_PERFLIMIT_NONE);

	acpi_perflimit_detach(&cpus[0]);
	CHECK(allocations == 0);
	CHECK(acpi_perflimit_set(&cpus[0], &a, 50) == ENODEV);
	CHECK(lock_depth == 0);
}

static struct provider zone_cpus[MAXCPUS];

static void
zone_init(struct acpitz_softc *sc, struct cpu_info **psl)
{
	memset(sc, 0, sizeof(*sc));
	sc->sc_psl = psl;
	sc->sc_passive = ACPI_PERFLIMIT_NONE;
	sc->sc_zone.tzp = 300;
	sc->sc_zone.tsp = 10;
	sc->sc_zone.psv = ATZ_ZEROC + 850;
	sc->sc_zone.tc1 = 2;
	sc->sc_zone.tc2 = 5;
	sc->sc_zone.tmp = ATZ_ZEROC + 400;
	sc->sc_zone.prevtmp = sc->sc_zone.tmp;
}

/* One sample in 0.1 C, as acpitz_get_status records and ends it. */
static unsigned int next_sample;

static void
sample(struct acpitz_softc *sc, unsigned int tenth_c)
{
	sc->sc_zone.prevtmp = sc->sc_zone.tmp;
	sc->sc_zone.tmp = ATZ_ZEROC + tenth_c;
	acpitz_passive(sc);
	acpitz_reschedule(sc);
}

static void
acpitz_get_status(void *opaque)
{
	sample(opaque, next_sample);
}

static void
zone_contract(void)
{
	struct cpu_info *psl[] = { &cpus[1], &cpus[2], NULL };
	struct cpu_info *other[] = { &cpus[2], NULL };
	struct acpitz_softc sc, sc2;
	struct sysctlnode node;
	unsigned int i, calls, messages;

	for (i = 1; i < MAXCPUS; i++) {
		zone_cpus[i].limit = ACPI_PERFLIMIT_NONE;
		CHECK(acpi_perflimit_attach(&cpus[i], provider_apply,
		    &zone_cpus[i]) == 0);
	}

	/* Below _PSV nothing is requested. */
	zone_init(&sc, psl);
	sample(&sc, 800);
	sample(&sc, 840);
	CHECK(sc.sc_passive == ACPI_PERFLIMIT_NONE && zone_cpus[1].calls == 0);

	/* dP = 2 * 2.0 K + 5 * 1.0 K = 9 %. */
	messages = normal_messages;
	sample(&sc, 860);
	CHECK(sc.sc_passive == 91);
	CHECK(zone_cpus[1].limit == 91 && zone_cpus[2].limit == 91);
	CHECK(normal_messages == messages + 1);

	/* A fast rise is bounded to ATZ_PASSIVE_STEP per sample. */
	sample(&sc, 960);
	CHECK(sc.sc_passive == 91 - ATZ_PASSIVE_STEP);

	/* A steady excess keeps lowering the limit, down to zero. */
	for (i = 0; i < 10; i++)
		sample(&sc, 960);
	CHECK(sc.sc_passive == 0 && zone_cpus[1].limit == 0);
	sample(&sc, 970);
	CHECK(sc.sc_passive == 0);

	/* A small rise above _PSV still lowers the limit by one percent. */
	zone_init(&sc2, other);
	sc2.sc_zone.tc1 = 0;
	sc2.sc_zone.tc2 = 1;
	sample(&sc2, 851);
	CHECK(sc2.sc_passive == 99);

	/* The stricter of two zones wins on a shared processor. */
	CHECK(zone_cpus[2].limit == 0);
	sample(&sc, 860);
	sample(&sc, 850);
	sample(&sc, 840);
	CHECK(sc.sc_passive > 0 && zone_cpus[2].limit == MIN(sc.sc_passive, 99));
	acpitz_passive_release(&sc2);
	CHECK(sc2.sc_passive == ACPI_PERFLIMIT_NONE);
	CHECK(zone_cpus[2].limit == sc.sc_passive);

	/* Below _PSV the processors recover and are released. */
	messages = normal_messages;
	for (i = 0; i < 200 && sc.sc_passive < ACPI_PERFLIMIT_NONE; i++)
		sample(&sc, 840);
	CHECK(sc.sc_passive == ACPI_PERFLIMIT_NONE);
	CHECK(zone_cpus[1].limit == ACPI_PERFLIMIT_NONE);
	CHECK(zone_cpus[2].limit == ACPI_PERFLIMIT_NONE);
	CHECK(normal_messages == messages + 1);
	calls = zone_cpus[1].calls;
	sample(&sc, 830);
	CHECK(zone_cpus[1].calls == calls);

	/* Without _TC1, _TC2 or _PSL the zone does not limit processors. */
	zone_init(&sc2, psl);
	sc2.sc_zone.tc1 = ATZ_TMP_INVALID;
	sample(&sc2, 990);
	CHECK(sc2.sc_passive == ACPI_PERFLIMIT_NONE);
	zone_init(&sc2, NULL);
	sample(&sc2, 990);
	CHECK(sc2.sc_passive == ACPI_PERFLIMIT_NONE);

	/* A missing provider is reported once and retried. */
	acpi_perflimit_detach(&cpus[3]);
	{
		struct cpu_info *missing[] = { &cpus[3], NULL };

		zone_init(&sc2, missing);
		messages = error_messages;
		sample(&sc2, 900);
		sample(&sc2, 910);
		CHECK(error_messages == messages + 1 && sc2.sc_passive_failed);
		zone_cpus[3].limit = ACPI_PERFLIMIT_NONE;
		CHECK(acpi_perflimit_attach(&cpus[3], provider_apply,
		    &zone_cpus[3]) == 0);
		sample(&sc2, 910);
		CHECK(!sc2.sc_passive_failed);
		CHECK(zone_cpus[3].limit == sc2.sc_passive);
		acpitz_passive_release(&sc2);
		CHECK(zone_cpus[3].limit == ACPI_PERFLIMIT_NONE);
	}

	/* hw.acpitzN.passive_limit reports the zone's request. */
	sc.sc_passive = 42;
	node.sysctl_num = 1;
	node.sysctl_data = &sc;
	CHECK(acpitz_sysctl_passive(&node, NULL) == 0 && read_value == 42);
	sc.sc_passive = ACPI_PERFLIMIT_NONE;

	/* The zone samples at _TSP near _PSV and while it limits. */
	zone_init(&sc, psl);
	CHECK(acpitz_poll_period(&sc) == 300 * hz / 10);
	sc.sc_zone.tmp = sc.sc_zone.psv - ATZ_PASSIVE_NEAR;
	CHECK(acpitz_poll_period(&sc) == 10 * hz / 10);
	sc.sc_zone.tmp = ATZ_ZEROC + 400;
	sc.sc_passive = 50;
	CHECK(acpitz_poll_period(&sc) == 10 * hz / 10);
	sc.sc_zone.tsp = ATZ_TMP_INVALID;
	CHECK(acpitz_poll_period(&sc) == 300 * hz / 10);
	sc.sc_zone.tsp = 0;
	CHECK(acpitz_poll_period(&sc) == 300 * hz / 10);
	sc.sc_zone.tsp = 600;
	CHECK(acpitz_poll_period(&sc) == 300 * hz / 10);
	sc.sc_zone.tsp = 10;
	sc.sc_psl = NULL;
	CHECK(acpitz_poll_period(&sc) == 300 * hz / 10);
	sc.sc_zone.tzp = 0;
	CHECK(acpitz_poll_period(&sc) == 1);

	/*
	 * acpitz_tick picks the next period before its queued sample runs.
	 * A sample that crosses _PSV must start _TSP sampling at once.
	 */
	zone_init(&sc, psl);
	sc.sc_ticking = true;
	order[0] = '\0';
	acpitz_tick(&sc);
	CHECK(strcmp(order, "sq") == 0 && scheduled == 300 * hz / 10);
	next_sample = 860;
	schedules = 0;
	queued(&sc);
	CHECK(sc.sc_passive < ACPI_PERFLIMIT_NONE);
	CHECK(schedules == 1 && scheduled == 10 * hz / 10);
	/* Later samples keep _TSP; no reschedule repeats the same period. */
	acpitz_tick(&sc);
	queued(&sc);
	CHECK(scheduled == 10 * hz / 10 && schedules == 2);
	/* Entering the band below _PSV also shortens the pending poll. */
	acpitz_passive_release(&sc);
	zone_init(&sc, psl);
	sc.sc_ticking = true;
	acpitz_tick(&sc);
	next_sample = 800;
	queued(&sc);
	CHECK(sc.sc_passive == ACPI_PERFLIMIT_NONE && scheduled == 10 * hz / 10);
	/* A recovered zone returns to _TZP on the next tick. */
	sc.sc_zone.tmp = ATZ_ZEROC + 400;
	acpitz_tick(&sc);
	CHECK(scheduled == 300 * hz / 10);
	/* A stopping or not yet started zone neither polls nor reschedules. */
	sc.sc_ticking = false;
	schedules = 0;
	queued = NULL;
	acpitz_tick(&sc);
	CHECK(schedules == 0 && queued == NULL);
	sc.sc_zone.tmp = sc.sc_zone.psv;
	acpitz_reschedule(&sc);
	CHECK(schedules == 0);
	acpitz_passive_release(&sc);

	for (i = 1; i < MAXCPUS; i++)
		acpi_perflimit_detach(&cpus[i]);
	CHECK(allocations == 0 && lock_depth == 0);
}

static int
cppc_sysctl(struct cppc_softc *sc, int num, const u_int *value)
{
	struct sysctlnode node = { num, sc };

	return cppc_cpufreq_sysctl(&node, value);
}

static void
cppc_contract(void)
{
	struct cppc_softc sc;
	u_int value;

	memset(&sc, 0, sizeof(sc));
	mutex_init(&sc.sc_lock, MUTEX_DEFAULT, IPL_NONE);
	sc.sc_freq_range = 1;
	sc.sc_perf_range = 1;
	sc.sc_node_target = 10;
	sc.sc_node_current = 11;
	sc.sc_lowest_perf = sc.sc_min_target = 800;
	sc.sc_highest_perf = sc.sc_max_target = 2600;
	sc.sc_user_perf = desired = 1500;
	sc.sc_limit = ACPI_PERFLIMIT_NONE;

	/* A limit above the user's target leaves it unchanged. */
	CHECK(cppc_perflimit_apply(&sc, 50) == 0 && desired == 1500);
	/* 800 + 1800 * 20 % = 1160. */
	CHECK(cppc_perflimit_apply(&sc, 20) == 0 && desired == 1160);
	CHECK(cppc_perflimit_apply(&sc, 0) == 0 && desired == 800);
	CHECK(cppc_perflimit_apply(&sc, 20) == 0 && desired == 1160);

	/* target is the user's request; current is the register. */
	CHECK(cppc_sysctl(&sc, 10, NULL) == 0 && read_value == 1500);
	CHECK(cppc_sysctl(&sc, 11, NULL) == 0 && read_value == 1160);

	/* A new target applies through the limit and after its release. */
	value = 2600;
	CHECK(cppc_sysctl(&sc, 10, &value) == 0);
	CHECK(sc.sc_user_perf == 2600 && desired == 1160);
	CHECK(cppc_perflimit_apply(&sc, ACPI_PERFLIMIT_NONE) == 0);
	CHECK(desired == 2600);
	value = 700;
	CHECK(cppc_sysctl(&sc, 10, &value) == EINVAL && sc.sc_user_perf == 2600);

	/* A failed write restores the previous state. */
	failed_write = true;
	CHECK(cppc_perflimit_apply(&sc, 10) == EIO);
	CHECK(sc.sc_limit == ACPI_PERFLIMIT_NONE && desired == 2600);
	value = 1000;
	CHECK(cppc_sysctl(&sc, 10, &value) == EIO && sc.sc_user_perf == 2600);
	failed_write = false;

	/* An unusable range leaves the user's target. */
	sc.sc_lowest_perf = sc.sc_highest_perf;
	CHECK(cppc_perflimit_apply(&sc, 10) == 0 && desired == 2600);
	CHECK(sc.sc_lock == 0 && writes > 0);
}

int
main(void)
{
	broker_contract();
	zone_contract();
	cppc_contract();
	printf("acpi passive cooling: %u checks\n", checks);
	return 0;
}
