/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 The EmberBSD Project
 *
 * Video sensor subdevice contract used across FDT media graph endpoints.
 */

#ifndef _SYS_DEV_VIDEO_SENSOR_IF_H_
#define _SYS_DEV_VIDEO_SENSOR_IF_H_

#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/types.h>
#else
#include <stdbool.h>
#include <errno.h>
#endif

struct video_format;

struct video_sensor_ops {
	/* These operations may sleep; callers must use process context. */
	/* get/try describe formats; they do not imply sensor initialization. */
	int	(*get_format)(void *, struct video_format *);
	int	(*try_format)(void *, struct video_format *);
	int	(*set_format)(void *, const struct video_format *);
	int	(*set_stream)(void *, bool);
};

struct video_sensor {
	const struct video_sensor_ops *ops;
	void	*cookie;
};

static inline int
video_sensor_get_format(struct video_sensor *sensor,
    struct video_format *format)
{
	if (sensor == NULL || format == NULL)
		return EINVAL;
	if (sensor->ops == NULL || sensor->ops->get_format == NULL)
		return ENOTSUP;
	return sensor->ops->get_format(sensor->cookie, format);
}

static inline int
video_sensor_try_format(struct video_sensor *sensor,
    struct video_format *format)
{
	if (sensor == NULL || format == NULL)
		return EINVAL;
	if (sensor->ops == NULL || sensor->ops->try_format == NULL)
		return ENOTSUP;
	return sensor->ops->try_format(sensor->cookie, format);
}

static inline int
video_sensor_set_format(struct video_sensor *sensor,
    const struct video_format *format)
{
	if (sensor == NULL || format == NULL)
		return EINVAL;
	if (sensor->ops == NULL || sensor->ops->set_format == NULL)
		return ENOTSUP;
	return sensor->ops->set_format(sensor->cookie, format);
}

static inline int
video_sensor_set_stream(struct video_sensor *sensor, bool enable)
{
	if (sensor == NULL)
		return EINVAL;
	if (sensor->ops == NULL || sensor->ops->set_stream == NULL)
		return ENOTSUP;
	return sensor->ops->set_stream(sensor->cookie, enable);
}

#endif /* _SYS_DEV_VIDEO_SENSOR_IF_H_ */
