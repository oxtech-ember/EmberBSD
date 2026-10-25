/* SPDX-License-Identifier: BSD-2-Clause */
/* AI-assisted EmberBSD model. No GPIO register or ACPI method is accessed. */
@SOURCE_COMMENTS@
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define AE_OK 0
#define AE_ERROR 1
#define AE_NO_MEMORY 4
#define AE_NOT_FOUND 5
#define AE_BAD_PARAMETER 0x1001
#define ACPI_FAILURE(status) ((status) != AE_OK)

static unsigned event_status, interrupt_calls, child_calls, provider_calls;
static unsigned cleanup_calls, error_calls;
static int interrupt_ok, child_ok, token;

/* These helpers count the calls in the inserted production tail. */
#define acpi_event_create_gpio(...) (event_status)
#define acpi_intr_establish(...) \
	(interrupt_calls++, interrupt_ok ? &token : NULL)
#define config_found(self, args, print, attributes) \
	(child_calls++, (args)->gba_gc != NULL && (args)->gba_pins != NULL && \
	(args)->gba_npins == 184 && child_ok ? &token : NULL)
#define acpi_gpio_register(...) (provider_calls++)
#define acpi_resource_cleanup(...) (cleanup_calls++)
#define aprint_error_dev(...) (error_calls++)

struct fixture_config {
	unsigned num_pins;
};
struct fixture_softc {
	void *sc_gpiodev, *sc_pins;
	int sc_gc;
	const struct fixture_config *sc_config;
};
struct fixture_gba {
	void *gba_gc, *gba_pins;
	unsigned gba_npins;
};

static void
attach_tail(void)
{
	const struct fixture_config config = { 184 };
	struct fixture_softc storage = { .sc_config = &config, .sc_pins = &token };
	struct fixture_softc *sc = &storage;
	struct fixture_gba gba;
	unsigned rv;
	const void *ih;

	@ATTACH_TAIL@
}

static int
check(unsigned status, int irq_ok, int gpio_ok, unsigned want_irq,
    unsigned want_child, unsigned want_provider, unsigned want_errors,
    const char *label)
{

	event_status = status;
	interrupt_ok = irq_ok;
	child_ok = gpio_ok;
	interrupt_calls = child_calls = provider_calls = cleanup_calls = error_calls = 0;
	attach_tail();
	if (interrupt_calls != want_irq || child_calls != want_child ||
	    provider_calls != want_provider || error_calls != want_errors ||
	    cleanup_calls != 1) {
		fprintf(stderr, "FAIL: %s\n", label);
		return 1;
	}
	return 0;
}

int
main(void)
{

	if (check(AE_OK, 1, 1, 1, 1, 1, 0, "events present") ||
	    check(AE_NOT_FOUND, 1, 1, 1, 1, 1, 0,
	        "absent _AEI must retain the GPIO provider") ||
	    check(AE_ERROR, 1, 1, 0, 0, 0, 1,
	        "event errors must abort before interrupt and GPIO registration") ||
	    check(AE_NO_MEMORY, 1, 1, 0, 0, 0, 1, "event allocation error") ||
	    check(AE_BAD_PARAMETER, 1, 1, 0, 0, 0, 1, "event argument error") ||
	    check(AE_OK, 0, 1, 1, 0, 0, 1, "interrupt failure with events") ||
	    check(AE_NOT_FOUND, 0, 1, 1, 0, 0, 1, "interrupt failure without events") ||
	    check(AE_OK, 1, 0, 1, 1, 0, 0, "missing GPIO child with events") ||
	    check(AE_NOT_FOUND, 1, 0, 1, 1, 0, 0, "missing GPIO child without events"))
		return 1;
	puts("AMD GPIO optional _AEI execution contract: PASS (9 cases)");
	return 0;
}
