/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: deployed EmberBSD Pi 5 fallback, source recorded in README.md. */
/*
 * The Raspberry Pi 5 firmware used with ACPI does not expose its power
 * button. GPIO20 in BCM2712's main bank is active low on Pi 5 and CM5.
 * Use device mappings and the standard power-switch event path. Polling
 * does not take ownership of the GPIO interrupt controller or change pins.
 */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/pmf.h>
#include <sys/systm.h>
#include <sys/workqueue.h>
#include <dev/sysmon/sysmonvar.h>

MODULE(MODULE_CLASS_DRIVER, rpi5button, "sysmon_power");

#define RPI5BUTTON_GPIO_BASE	0x107d508500ULL
#define RPI5BUTTON_GPIO_SIZE	0x10
#define RPI5BUTTON_DATA		4
#define RPI5BUTTON_DIRECTION	8
#define RPI5BUTTON_BIT		__BIT(20)
#define RPI5BUTTON_SAMPLES	5

extern struct bus_space arm_generic_bs_tag;
static bus_space_tag_t bst = &arm_generic_bs_tag;
static bus_space_handle_t gpio;
static struct callout poller;
static struct workqueue *queue;
static struct work event_work;
static struct sysmon_pswitch button = {
	.smpsw_name = "rpi5button",
	.smpsw_type = PSWITCH_TYPE_POWER,
};
static bool candidate, armed, fired;
static unsigned int samples;

static void
rpi5button_event(struct work *work, void *arg)
{

	printf("rpi5button: power button pressed\n");
	sysmon_pswitch_event(&button, PSWITCH_EVENT_PRESSED);
}

static void
rpi5button_poll(void *arg)
{
	bool released;

	if (!fired) {
		released = (bus_space_read_4(bst, gpio, RPI5BUTTON_DATA) &
		    RPI5BUTTON_BIT) != 0;
		if (released != candidate) {
			candidate = released;
			samples = 1;
		} else if (samples < RPI5BUTTON_SAMPLES) {
			samples++;
		}
		if (samples == RPI5BUTTON_SAMPLES) {
			if (released) {
				/* A held boot button must be released before arming. */
				armed = true;
			} else if (armed) {
				/* One request is enough; shutdown can take time. */
				fired = true;
				workqueue_enqueue(queue, &event_work, NULL);
			}
		}
	}
	callout_schedule(&poller, MAX(1, hz / 100));
}

static int
rpi5button_modcmd(modcmd_t cmd, void *arg)
{
	const char *product;
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
		product = pmf_get_platform("system-product");
		if (product == NULL ||
		    (strcmp(product, "Raspberry Pi 5 Model B") != 0 &&
		    strcmp(product, "Raspberry Pi Compute Module 5") != 0))
			return ENXIO;
		error = bus_space_map(bst, RPI5BUTTON_GPIO_BASE,
		    RPI5BUTTON_GPIO_SIZE, 0, &gpio);
		if (error != 0)
			return error;
		if ((bus_space_read_4(bst, gpio, RPI5BUTTON_DIRECTION) &
		    RPI5BUTTON_BIT) == 0) {
			error = ENXIO;
			goto unmap;
		}
		error = workqueue_create(&queue, "rpi5button", rpi5button_event,
		    NULL, PRI_NONE, IPL_SOFTCLOCK, WQ_MPSAFE);
		if (error != 0)
			goto unmap;
		error = sysmon_pswitch_register(&button);
		if (error != 0) {
			workqueue_destroy(queue);
			goto unmap;
		}
		candidate = armed = fired = false;
		samples = 0;
		callout_init(&poller, CALLOUT_MPSAFE);
		callout_setfunc(&poller, rpi5button_poll, NULL);
		callout_schedule(&poller, MAX(1, hz / 100));
		printf("rpi5button: BCM2712 GPIO20 power button\n");
		return 0;
unmap:
		bus_space_unmap(bst, gpio, RPI5BUTTON_GPIO_SIZE);
		return error;
	case MODULE_CMD_FINI:
		/* Drain deferred events before their code or switch disappears. */
		callout_halt(&poller, NULL);
		callout_destroy(&poller);
		workqueue_wait(queue, &event_work);
		workqueue_destroy(queue);
		sysmon_pswitch_unregister(&button);
		bus_space_unmap(bst, gpio, RPI5BUTTON_GPIO_SIZE);
		return 0;
	case MODULE_CMD_AUTOUNLOAD:
		return EBUSY;
	default:
		return ENOTTY;
	}
}
