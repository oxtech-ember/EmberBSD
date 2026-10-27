/*
 * Raspberry Pi 3 B: the Bluetooth controller's power pin (BT_ON) is line 0
 * of the firmware's GPIO expander, which only the VideoCore mailbox reaches.
 * Loading this module power-cycles the controller and leaves it on;
 * unloading powers it off.  After boot the controller answers HCI only
 * after BT_ON has gone low and high again.
 * Pi 3 B's devicetree names the line "BT_ON" in gpio-line-names, index 0.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/proc.h>

#include <arm/broadcom/bcm2835_mbox.h>
#include <evbarm/rpi/vcio.h>
#include <evbarm/rpi/vcprop.h>

MODULE(MODULE_CLASS_MISC, bcmexpgpio, NULL);

#define	EXPGPIO_BASE	128	/* firmware numbering of the expander's lines */
#define	EXPGPIO_BT_ON	0

static struct {
	struct vcprop_buffer_hdr vb_hdr;
	struct {
		struct vcprop_tag tag;
		uint32_t gpio;	/* the firmware answers with 0 here on success */
		uint32_t state;
	} gpio;
	struct vcprop_tag end;
} vb __cacheline_aligned;

static int
bcmexpgpio_set(unsigned int line, unsigned int state)
{
	uint32_t res;
	int error;

	VCPROP_INIT_REQUEST(vb);
	VCPROP_INIT_TAG(vb.gpio, VCPROPTAG_SET_GPIO_STATE);
	vb.gpio.gpio = htole32(EXPGPIO_BASE + line);
	vb.gpio.state = htole32(state);
	error = bcmmbox_request(BCMMBOX_CHANARM2VC, &vb, sizeof(vb), &res);
	if (error != 0)
		return error;
	/* The tag's own response bit stays clear for this request. */
	if (!vcprop_buffer_success_p(&vb.vb_hdr) || vb.gpio.gpio != 0)
		return EIO;
	return 0;
}

static int
bcmexpgpio_modcmd(modcmd_t cmd, void *arg)
{
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
		error = bcmexpgpio_set(EXPGPIO_BT_ON, 0);
		if (error != 0)
			return error;
		/* Held low for a second, as on the bench. */
		kpause("btpower", false, hz, NULL);
		return bcmexpgpio_set(EXPGPIO_BT_ON, 1);
	case MODULE_CMD_FINI:
		return bcmexpgpio_set(EXPGPIO_BT_ON, 0);
	default:
		return ENOTTY;
	}
}
