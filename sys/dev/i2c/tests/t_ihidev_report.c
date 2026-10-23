/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/endian.h>
#include <dev/hid/hid.h>
#include <sys/kmem.h>
static void
test_noop(void)
{
}

size_t hidmt_test_allocations;
#define KM_NOSLEEP 1
#define I2C_F_POLL 2
#define I2C_OP_READ_WITH_STOP 1
#define I2C_OP_WRITE_WITH_STOP 2
#define I2C_HID_REPORT_TYPE_INPUT 1
#define I2C_HID_REPORT_TYPE_OUTPUT 2
#define I2C_HID_REPORT_TYPE_FEATURE 3
#define DPRINTF(x) test_noop()
#define aprint_error_dev(...) test_noop()
#define device_private(d) ((d)->private)
struct device {
	void *private;
};
struct i2c_hid_report_request {
	unsigned id, type;
	void *data;
	unsigned len;
};
struct i2c_hid_desc {
	uint16_t wHIDDescLength, bcdVersion, wReportDescLength, wReportDescRegister, wInputRegister,
	         wMaxInputLength, wOutputRegister, wMaxOutputLength, wCommandRegister,
	         wDataRegister, wVendorID, wProductID, wVersionID;
	uint32_t reserved;
} __attribute__((packed));
struct ihidev_softc {
	struct device *sc_dev;
	int sc_tag, sc_addr;
	unsigned sc_hid_desc_addr;
	union {
		uint8_t hid_desc_buf[sizeof(struct i2c_hid_desc)];
		struct i2c_hid_desc hid_desc;
	};
	void *sc_report;
	unsigned sc_reportlen;
};
static bool held, bad_id;
static int bus_flags;
static int
iic_acquire_bus(int tag, int flags)
{
	assert(!held);
	held = true;
	bus_flags = flags;
	return 0;
}
static void
iic_release_bus(int tag, int flags)
{
	assert(held && flags == bus_flags);
	held = false;
}
static int
iic_exec(int tag, int op, int addr, const void *cmd, size_t cn, void *out, size_t n, int flags)
{
	assert(held && flags == bus_flags);
	const uint8_t *c = cmd;
	if (op == I2C_OP_READ_WITH_STOP) {
		uint8_t *p = out;
		memset(p, 0, n);
		p[0] = n;
		p[1] = n >> 8;
		p[2] = bad_id ? 99 : (c[2] & 15);
		if (n > 3)
			p[3] = 0xa0;
	} else {
		assert(cn == 10 && c[2] == 0x33 && c[9] == 0xa3);
	}
	return 0;
}
int ihidev_get_report_poll(struct device *, int, int, void *, int, bool);
int ihidev_set_report_poll(struct device *, int, int, void *, int, bool);
#include "commands.c"
int
main(void)
{
	struct ihidev_softc sc = {0};
	struct device dev = {.private = &sc};
	uint8_t data = 0;
	sc.sc_dev = &dev;
	for (unsigned poll = 0; poll < 2; poll++) {
		assert(ihidev_get_report_poll(&dev, hid_feature, 3, &data, 1, poll) == 0);
		assert(data == 0xa0 && !held && bus_flags == (poll ? I2C_F_POLL : 0));
		data = 0xa3;
		assert(ihidev_set_report_poll(&dev, hid_feature, 3, &data, 1, poll) == 0);
		assert(!held && bus_flags == (poll ? I2C_F_POLL : 0));
		bad_id = true;
		assert(ihidev_get_report_poll(&dev, hid_feature, 3, &data, 1, poll) != 0);
		assert(!held);
		bad_id = false;
	}
	assert(ihidev_get_report(&dev, hid_feature, 3, &data, 1) == 0 && bus_flags == 0);
	data = 0xa3;
	assert(ihidev_set_report(&dev, hid_feature, 3, &data, 1) == 0 && bus_flags == 0);
	assert(hidmt_test_allocations == 0);
	puts("ihidev polled report contract: PASS");
	return 0;
}
