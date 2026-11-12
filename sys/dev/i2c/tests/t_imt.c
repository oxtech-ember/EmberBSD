/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD direct Precision Touchpad mode regression. */
#include "imt_doubles.h"
#include "adapter.c"
int
main(void)
{
	struct imt_softc sc = {0};
	struct device child = {.private = &sc}, mouse = {0}, parent = {0};
	struct ihidev_softc bus = {.sc_dev = &parent};
	struct ihidev_attach_arg aux = {.parent = &bus, .reportid = 4};
	parent.private = &bus;
	test_mouse = &mouse;
	imt_attach(&parent, &child, &aux);
	assert(suspend_cb && resume_cb);
	/* A mode-only report can be written directly, without a GET. */
	set_error = EIO;
	assert(imt_enable(&sc) == EIO && bus.refs == 0);
	assert(get_calls == 0 && set_calls == 1 && last_set_mode == 0x03);
	set_error = 0;
	assert(imt_enable(&sc) == 0 && bus.refs == 1 && bus.mode == 3);
	assert(get_calls == 0 && set_calls == 2 && last_set_mode == 0x03);
	assert(imt_enable(&sc) == EBUSY && bus.refs == 1);
	sc.sc_pointer.tap_candidate = true;
	assert(suspend_cb(&child, NULL));
	assert(!sc.sc_pointer.tap_candidate && last_buttons == 0);
	assert(imt_enable(&sc) == EBUSY && bus.refs == 1);
	unsigned before = events;
	imt_intr(&sc.sc_hdev, NULL, 0);
	assert(events == before);
	bus.mode = 0;		/* Parent resumes first and resets the device
				 * to mouse mode. */
	expect_poll = true;
	assert(resume_cb(&child, NULL));
	assert(bus.mode == 3 && bus.refs == 1);
	assert(get_calls == 0 && last_set_mode == 0x03);
	imt_intr(&sc.sc_hdev, NULL, 0);
	assert(events == before + 1 && last_buttons == 0);
	assert(suspend_cb(&child, NULL));
	bus.mode = 0;
	set_error = EIO;
	assert(!resume_cb(&child, NULL) && bus.mode == 0 && bus.refs == 1);
	before = events;
	imt_intr(&sc.sc_hdev, NULL, 0);
	assert(events == before);
	set_error = 0;
	get_error = EIO;
	assert(resume_cb(&child, NULL) && bus.mode == 3 && bus.refs == 1);
	assert(get_calls == 0 && last_set_mode == 0x03);
	get_error = 0;
	sc.sc_pointer.tap_candidate = true;
	assert(resume_cb(&child, NULL) && bus.mode == 3 && bus.refs == 1);
	assert(!sc.sc_pointer.tap_candidate);
	/* A composite report must preserve its unrelated 0xa0 feature bits. */
	sc.sc_config.config_mode_only = false;
	assert(suspend_cb(&child, NULL));
	bus.mode = 0;
	{
		unsigned gets = get_calls, sets = set_calls;

		assert(resume_cb(&child, NULL) && bus.mode == 3 && bus.refs == 1);
		assert(get_calls == gets + 1 && set_calls == sets + 1);
		assert(last_set_mode == 0xa3);
	}
	assert(suspend_cb(&child, NULL));
	bus.mode = 0;
	get_error = EIO;
	{
		unsigned gets = get_calls, sets = set_calls;

		assert(!resume_cb(&child, NULL) && bus.mode == 0 && bus.refs == 1);
		assert(get_calls == gets + 1 && set_calls == sets);
	}
	get_error = 0;
	{
		unsigned gets = get_calls, sets = set_calls;

		assert(resume_cb(&child, NULL) && bus.mode == 3 && bus.refs == 1);
		assert(get_calls == gets + 1 && set_calls == sets + 1);
		assert(last_set_mode == 0xa3);
	}
	imt_disable(&sc);
	assert(bus.refs == 0);
	assert(suspend_cb(&child, NULL));
	bus.mode = 0;
	assert(resume_cb(&child, NULL) && bus.mode == 0 && bus.refs == 0);
	expect_poll = false;
	get_error = EIO;
	{
		unsigned gets = get_calls, sets = set_calls;

		assert(imt_enable(&sc) == EIO && bus.refs == 0);
		assert(get_calls == gets + 1 && set_calls == sets);
	}
	get_error = 0;
	{
		unsigned gets = get_calls, sets = set_calls;

		assert(imt_enable(&sc) == 0 && bus.refs == 1);
		assert(get_calls == gets + 1 && set_calls == sets + 1);
		assert(last_set_mode == 0xa3);
	}
	assert(suspend_cb(&child, NULL));
	imt_disable(&sc);
	assert(bus.refs == 0);
	expect_poll = true;
	bus.mode = 0;
	assert(resume_cb(&child, NULL) && bus.mode == 0 && bus.refs == 0);
	assert(imt_detach(&child, 0) == 0);
	assert(hidmt_test_allocations == 0);
	puts("imt adapter suspend/resume contract: PASS");
	return 0;
}
