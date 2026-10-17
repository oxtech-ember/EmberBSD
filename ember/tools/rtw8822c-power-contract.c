/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: actual-source PCI3081 MAC power setup contract, AI-assisted. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include "rtw8822c-power-model.h"
int rtw8822c_power_pre(struct rtw88_softc *);
int rtw8822c_power_post(struct rtw88_softc *);
static unsigned assertions, operations, fail_at, cancel_at, cancel_kind;
static unsigned after_error, after_cancel;
static int failed;
static struct rtw88_softc *active;
#define CHECK(x) do { assertions++; if (!(x)) { \
	fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); exit(1); \
} } while (0)
static void
reset(struct rtw88_softc *sc, unsigned seed, unsigned flash)
{

	model_reset(seed, flash);
	operations = fail_at = cancel_at = cancel_kind = 0;
	after_error = after_cancel = 0;
	failed = 0;
	active = sc;
	sc->sc_stop_requested = 0;
	sc->sc_generation = 19;
}
static int
operation(void)
{

	if (failed)
		after_error++;
	if (active->sc_stop_requested || active->sc_generation != 19)
		after_cancel++;
	operations++;
	if (operations == fail_at) {
		failed = 1;
		return EIO;
	}
	return 0;
}
static void
finish(void)
{

	if (operations == cancel_at) {
		if (cancel_kind == 1)
			active->sc_stop_requested = 1;
		else
			active->sc_generation++;
	}
}
static int
rd1(void *cookie, uint32_t address, uint8_t *value)
{

	(void)cookie;
	if (operation())
		return EIO;
	*value = (uint8_t)model_read(address, 1);
	finish();
	return 0;
}
static int
rd4(void *cookie, uint32_t address, uint32_t *value)
{

	(void)cookie;
	if (operation())
		return EIO;
	*value = model_read(address, 4);
	finish();
	return 0;
}
static int
wr1(void *cookie, uint32_t address, uint8_t value)
{

	(void)cookie;
	if (operation())
		return EIO;
	model_write(address, 1, value);
	finish();
	return 0;
}
static int
wr4(void *cookie, uint32_t address, uint32_t value)
{

	(void)cookie;
	if (operation())
		return EIO;
	model_write(address, 4, value);
	finish();
	return 0;
}
static const struct rtw88_bus_ops ops = {
	.read_1 = rd1, .read_4 = rd4, .write_1 = wr1, .write_4 = wr4
};
static int (*const stages[])(struct rtw88_softc *) = {
	rtw8822c_power_pre, rtw8822c_power_post
};
static void
compare(const char *path, unsigned stage, unsigned seed, unsigned flash)
{
	FILE *fp = fopen(path, "r");
	char line[128];
	unsigned s, p, f, count, n = 0, found = 0;
	int match = 0;

	CHECK(fp != NULL);
	while (fgets(line, sizeof(line), fp)) {
		CHECK(strchr(line, '\n') != NULL);
		line[strcspn(line, "\n")] = 0;
		if (sscanf(line, "stage %u seed %u flash %u events %u", &s, &p, &f, &count) == 4) {
			match = s == stage && p == seed && f == flash;
			if (match) {
				CHECK(found++ == 0 && count == event_count);
			}
			continue;
		}
		if (match && (line[0] == 'r' || line[0] == 'w')) {
			CHECK(n < event_count);
			if (strcmp(line, events[n]) != 0) {
				fprintf(stderr, "stage%u seed%u flash%u event%u want:%s got:%s\n",
				    stage, seed, flash, n, line, events[n]);
				exit(1);
			}
			n++;
		}
	}
	CHECK(ferror(fp) == 0);
	CHECK(fclose(fp) == 0);
	CHECK(found == 1 && n == event_count && n > 5);
}
int
main(int argc, char **argv)
{
	struct rtw88_softc sc = { .sc_ops = &ops, .sc_resources_ready = 1,
	    .sc_bootstrapping = 1 };
	unsigned stage, seed, flash, i, kind;

	CHECK(argc == 2);
	for (stage = 0; stage < 2; stage++) {
		for (seed = 0; seed < 3; seed++) for (flash = 0; flash < 2; flash++) {
			reset(&sc, seed, flash);
			CHECK(stages[stage](&sc) == 0);
			compare(argv[1], stage, seed, flash);
		}
		for (flash = 0; flash < 2; flash++) {
			unsigned total;

			reset(&sc, 2, flash);
			CHECK(stages[stage](&sc) == 0);
			total = operations;
			for (i = 1; i <= total; i++) {
				reset(&sc, 2, flash);
				fail_at = i;
				CHECK(stages[stage](&sc) == EIO);
				CHECK(operations == i && after_error == 0 && failed);
			}
			for (kind = 1; kind <= 2; kind++) for (i = 1; i <= total; i++) {
				reset(&sc, 2, flash);
				cancel_at = i;
				cancel_kind = kind;
				CHECK(stages[stage](&sc) == EINTR);
				CHECK(operations == i && after_cancel == 0);
			}
		}
		reset(&sc, 0, 0);
		sc.sc_stop_requested = 1;
		CHECK(stages[stage](&sc) == EINTR && operations == 0);
		reset(&sc, 0, 0);
		sc.sc_started = 1;
		CHECK(stages[stage](&sc) == EBUSY && operations == 0);
		sc.sc_started = 0;
		sc.sc_bootstrapping = 0;
		CHECK(stages[stage](&sc) == ENXIO && operations == 0);
		sc.sc_bootstrapping = 1;
		sc.sc_resources_ready = 0;
		CHECK(stages[stage](&sc) == ENXIO && operations == 0);
		sc.sc_resources_ready = 1;
		CHECK(stages[stage](NULL) == EINVAL);
		for (i = 0; i < 4; i++) {
			struct rtw88_bus_ops invalid = ops;

			if (i == 0) invalid.read_1 = NULL;
			if (i == 1) invalid.read_4 = NULL;
			if (i == 2) invalid.write_1 = NULL;
			if (i == 3) invalid.write_4 = NULL;
			sc.sc_ops = &invalid;
			CHECK(stages[stage](&sc) == EINVAL && operations == 0);
		}
		sc.sc_ops = NULL;
		CHECK(stages[stage](&sc) == EINVAL);
		sc.sc_ops = &ops;
	}
	printf("%u MAC power setup assertions passed\n", assertions);
	return 0;
}
