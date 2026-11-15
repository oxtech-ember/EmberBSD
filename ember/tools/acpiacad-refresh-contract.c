/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Run the production acpiacad(4) status and envsys refresh functions with
 * a controlled _PSR provider and clock.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AE_OK 0
#define AE_ERROR 1
#define AE_BAD_VALUE 2
#define ACPI_FAILURE(rv) ((rv) != AE_OK)
#define ENVSYS_SINVALID 0
#define ENVSYS_SVALID 1
#define PSWITCH_EVENT_PRESSED 1
#define PSWITCH_EVENT_RELEASED 2
#define device_private(dv) (dv)
#define time_uptime fake_uptime

typedef void *ACPI_HANDLE;
typedef int ACPI_STATUS;
typedef uint64_t ACPI_INTEGER;
typedef struct {
	bool held;
} kmutex_t;
typedef struct {
	int state;
	int64_t value_cur;
} envsys_data_t;
struct acpi_devnode {
	ACPI_HANDLE ad_handle;
};
struct sysmon_pswitch {
	int unused;
};
struct acpiacad_softc;
typedef struct acpiacad_softc *device_t;
struct sysmon_envsys {
	void *sme_cookie;
};

static const char *
AcpiFormatException(ACPI_STATUS rv)
{

	return rv == AE_OK ? "ok" : "error";
}

static void
aprint_debug_dev(device_t dv, const char *fmt, ...)
{

	(void)dv;
	(void)fmt;
}

#include "acpiacad-types.h"

/* The pre-fix source has no bound: let it fail by assertion, not by build. */
#ifndef ACPIACAD_REFRESH_SECS
#define ACPIACAD_REFRESH_SECS 5
#endif

static unsigned int checks, failures;
static unsigned int evaluations, pressed, released, unlocked_evaluations;
static unsigned int bad_names, relocks, bad_unlocks;
static ACPI_STATUS psr_rv;
static ACPI_INTEGER psr_value;
static time_t fake_uptime;
static int handle_cookie;

#define CHECK(c) do { \
	checks++; \
	if (!(c)) { \
		failures++; \
		fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #c); \
	} \
} while (0)

static void
mutex_enter(kmutex_t *mtx)
{

	if (mtx->held)
		relocks++;
	mtx->held = true;
}

static void
mutex_exit(kmutex_t *mtx)
{

	if (!mtx->held)
		bad_unlocks++;
	mtx->held = false;
}

static struct acpiacad_softc softc;

static ACPI_STATUS
acpi_eval_integer(ACPI_HANDLE handle, const char *name, ACPI_INTEGER *value)
{

	evaluations++;
	if (handle != &handle_cookie || strcmp(name, "_PSR") != 0)
		bad_names++;
	if (!softc.sc_mtx.held)
		unlocked_evaluations++;
	if (ACPI_FAILURE(psr_rv))
		return psr_rv;
	*value = psr_value;
	return AE_OK;
}

static void
sysmon_pswitch_event(const struct sysmon_pswitch *smpsw, int event)
{

	assert(smpsw == &softc.sc_smpsw);
	if (event == PSWITCH_EVENT_PRESSED)
		pressed++;
	else if (event == PSWITCH_EVENT_RELEASED)
		released++;
	else
		abort();
}

#include "acpiacad-functions.h"

static struct acpi_devnode node = { &handle_cookie };
static struct sysmon_envsys envsys = { &softc };

static void
reset(void)
{

	memset(&softc, 0, sizeof(softc));
	softc.sc_node = &node;
	softc.sc_status = -1;
	softc.sc_sensor.state = ENVSYS_SINVALID;
	evaluations = pressed = released = 0;
	psr_rv = AE_OK;
	psr_value = 1;
	fake_uptime = 1000;
}

static void
refresh(void)
{

	acpiacad_refresh(&envsys, &softc.sc_sensor);
}

/* The first request has no sample to reuse. */
static void
test_first_refresh(void)
{

	reset();
	refresh();
	CHECK(evaluations == 1);
	CHECK(softc.sc_sensor.state == ENVSYS_SVALID);
	CHECK(softc.sc_sensor.value_cur == 1);
	CHECK(softc.sc_status == 1);
	CHECK(pressed == 1 && released == 0);
}

/* The first request at uptime zero still evaluates. */
static void
test_first_refresh_at_boot(void)
{

	reset();
	fake_uptime = 0;
	refresh();
	CHECK(evaluations == 1);
	CHECK(softc.sc_sensor.state == ENVSYS_SVALID);
}

/* Requests inside the bound reuse the sample: no control method runs. */
static void
test_refresh_reuses_sample(void)
{
	unsigned int i;

	reset();
	refresh();
	for (i = 0; i < 100; i++)
		refresh();
	CHECK(evaluations == 1);
	fake_uptime += ACPIACAD_REFRESH_SECS;
	refresh();
	CHECK(evaluations == 1);
	CHECK(softc.sc_sensor.state == ENVSYS_SVALID);
	CHECK(softc.sc_sensor.value_cur == 1);
	CHECK(pressed == 1);
}

/* A sample older than the bound is read again, once. */
static void
test_refresh_after_bound(void)
{

	reset();
	refresh();
	fake_uptime += ACPIACAD_REFRESH_SECS + 1;
	refresh();
	CHECK(evaluations == 2);
	refresh();
	CHECK(evaluations == 2);
	CHECK(pressed == 1 && released == 0);
}

/* Notify, poll and resume always read _PSR and renew the sample. */
static void
test_status_always_evaluates(void)
{

	reset();
	acpiacad_get_status(&softc);
	acpiacad_get_status(&softc);
	CHECK(evaluations == 2);
	fake_uptime += ACPIACAD_REFRESH_SECS;
	acpiacad_get_status(&softc);
	CHECK(evaluations == 3);
	fake_uptime += ACPIACAD_REFRESH_SECS;
	refresh();
	CHECK(evaluations == 3);
	CHECK(pressed == 1);
}

/* With the default poll, frequent envsys requests add no evaluations. */
static void
test_poll_and_frequent_requests(void)
{
	unsigned int second, i, polls = 0;

	reset();
	for (second = 0; second < 60; second++) {
		if (second % 5 == 0) {
			acpiacad_get_status(&softc);
			polls++;
		}
		for (i = 0; i < 33; i++)
			refresh();
		fake_uptime++;
	}
	CHECK(polls == 12);
	CHECK(evaluations == polls);
	CHECK(softc.sc_sensor.state == ENVSYS_SVALID);
}

/* Without Notify and poll a request still sees the change, after the bound. */
static void
test_change_through_refresh(void)
{

	reset();
	refresh();
	psr_value = 0;
	refresh();
	CHECK(softc.sc_sensor.value_cur == 1);
	CHECK(released == 0);
	fake_uptime += ACPIACAD_REFRESH_SECS + 1;
	refresh();
	CHECK(evaluations == 2);
	CHECK(softc.sc_sensor.value_cur == 0);
	CHECK(softc.sc_status == 0);
	CHECK(pressed == 1 && released == 1);
	refresh();
	CHECK(released == 1);
}

/* A failed read marks the sensor invalid and is not retried inside the bound. */
static void
test_failure(void)
{

	reset();
	refresh();
	fake_uptime += ACPIACAD_REFRESH_SECS + 1;
	psr_rv = AE_ERROR;
	refresh();
	CHECK(evaluations == 2);
	CHECK(softc.sc_sensor.state == ENVSYS_SINVALID);
	CHECK(softc.sc_status == 1);
	refresh();
	refresh();
	CHECK(evaluations == 2);
	CHECK(softc.sc_sensor.state == ENVSYS_SINVALID);
	fake_uptime += ACPIACAD_REFRESH_SECS + 1;
	psr_rv = AE_OK;
	refresh();
	CHECK(evaluations == 3);
	CHECK(softc.sc_sensor.state == ENVSYS_SVALID);
	CHECK(softc.sc_sensor.value_cur == 1);
	CHECK(pressed == 1 && released == 0);
}

/* A failed first read is not retried inside the bound either. */
static void
test_first_failure(void)
{

	reset();
	psr_rv = AE_ERROR;
	refresh();
	refresh();
	CHECK(evaluations == 1);
	CHECK(softc.sc_sensor.state == ENVSYS_SINVALID);
	CHECK(softc.sc_status == -1);
	CHECK(pressed == 0 && released == 0);
}

/* A value other than 0 or 1 is a failed read. */
static void
test_bad_value(void)
{

	reset();
	psr_value = 2;
	acpiacad_get_status(&softc);
	CHECK(softc.sc_sensor.state == ENVSYS_SINVALID);
	CHECK(softc.sc_status == -1);
	CHECK(pressed == 0 && released == 0);
	psr_value = 0;
	acpiacad_get_status(&softc);
	CHECK(softc.sc_sensor.state == ENVSYS_SVALID);
	CHECK(softc.sc_status == 0);
	CHECK(released == 1);
}

int
main(void)
{

	test_first_refresh();
	test_first_refresh_at_boot();
	test_refresh_reuses_sample();
	test_refresh_after_bound();
	test_status_always_evaluates();
	test_poll_and_frequent_requests();
	test_change_through_refresh();
	test_failure();
	test_first_failure();
	test_bad_value();

	CHECK(ACPIACAD_REFRESH_SECS >= 1);
	CHECK(unlocked_evaluations == 0);
	CHECK(bad_names == 0);
	CHECK(relocks == 0);
	CHECK(bad_unlocks == 0);
	CHECK(!softc.sc_mtx.held);

	printf("acpiacad-refresh: %u checks, %u failures\n", checks, failures);
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
