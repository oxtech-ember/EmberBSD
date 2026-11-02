/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct video_format {
	uint32_t width;
	uint32_t height;
};

#include "../../video_sensor_if.h"

struct fixture {
	struct video_format format;
	unsigned int calls;
	bool streaming;
	int error;
};

static int
get_format(void *cookie, struct video_format *format)
{
	struct fixture *f = cookie;
	f->calls++;
	if (f->error != 0)
		return f->error;
	*format = f->format;
	return 0;
}

static int
set_format(void *cookie, const struct video_format *format)
{
	struct fixture *f = cookie;
	f->calls++;
	if (f->error != 0)
		return f->error;
	f->format = *format;
	return 0;
}

static int
set_stream(void *cookie, bool enable)
{
	struct fixture *f = cookie;
	f->calls++;
	if (f->error != 0)
		return f->error;
	f->streaming = enable;
	return 0;
}

int
main(void)
{
	static const struct video_sensor_ops ops = {
		.get_format = get_format,
		.set_format = set_format,
		.set_stream = set_stream,
	};
	struct fixture fixture = { .format = {640, 480} };
	struct video_sensor sensor = { .ops = &ops, .cookie = &fixture };
	struct video_format format = {0};

	assert(video_sensor_get_format(&sensor, &format) == 0);
	assert(format.width == 640 && format.height == 480 && fixture.calls == 1);
	format = (struct video_format){320, 240};
	assert(video_sensor_set_format(&sensor, &format) == 0);
	assert(fixture.format.width == 320 && fixture.format.height == 240);
	assert(video_sensor_set_stream(&sensor, true) == 0);
	assert(fixture.streaming && fixture.calls == 3);
	fixture.error = EIO;
	assert(video_sensor_get_format(&sensor, &format) == EIO);
	assert(video_sensor_set_stream(&sensor, false) == EIO);
	assert(video_sensor_get_format(NULL, &format) == EINVAL);
	return 0;
}
