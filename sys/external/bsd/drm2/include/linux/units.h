/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native implementation of the interface of Linux 6.18
 * include/linux/units.h (names and numeric meaning only; the Linux file is
 * GPL-2.0 and no source is copied).  Temperature helpers round to nearest,
 * ties away from zero.
 */

#ifndef _LINUX_UNITS_H_
#define _LINUX_UNITS_H_

#include <linux/types.h>

#define	PETA	1000000000000000ULL
#define	TERA	1000000000000ULL
#define	GIGA	1000000000UL
#define	MEGA	1000000UL
#define	KILO	1000UL
#define	HECTO	100UL
#define	DECA	10UL
#define	DECI	10UL
#define	CENTI	100UL
#define	MILLI	1000UL
#define	MICRO	1000000UL
#define	NANO	1000000000UL
#define	PICO	1000000000000ULL
#define	FEMTO	1000000000000000ULL

#define	NANOHZ_PER_HZ		1000000000UL
#define	MICROHZ_PER_HZ		1000000UL
#define	MILLIHZ_PER_HZ		1000UL
#define	HZ_PER_KHZ		1000UL
#define	HZ_PER_MHZ		1000000UL
#define	HZ_PER_GHZ		1000000000UL
#define	KHZ_PER_MHZ		1000UL
#define	KHZ_PER_GHZ		1000000UL

#define	MILLIWATT_PER_WATT	1000UL
#define	MICROWATT_PER_MILLIWATT	1000UL
#define	MICROWATT_PER_WATT	1000000UL

#define	BYTES_PER_KBIT		(KILO / 8)
#define	BYTES_PER_MBIT		(MEGA / 8)
#define	BYTES_PER_GBIT		(GIGA / 8)

#define	ABSOLUTE_ZERO_MILLICELSIUS	-273150

#define	MILLIDEGREE_PER_DEGREE		1000
#define	MILLIDEGREE_PER_DECIDEGREE	100

static inline long
_units_div_round_closest(long n, long d)
{

	return (n < 0 ? n - d / 2 : n + d / 2) / d;
}

static inline long
milli_kelvin_to_millicelsius(long t)
{

	return t + ABSOLUTE_ZERO_MILLICELSIUS;
}

static inline long
millicelsius_to_milli_kelvin(long t)
{

	return t - ABSOLUTE_ZERO_MILLICELSIUS;
}

static inline long
kelvin_to_millicelsius(long t)
{

	return milli_kelvin_to_millicelsius(t * MILLIDEGREE_PER_DEGREE);
}

static inline long
millicelsius_to_kelvin(long t)
{

	return _units_div_round_closest(millicelsius_to_milli_kelvin(t),
	    MILLIDEGREE_PER_DEGREE);
}

static inline long
deci_kelvin_to_millicelsius_with_offset(long t, long offset)
{

	return t * MILLIDEGREE_PER_DECIDEGREE + offset;
}

static inline long
deci_kelvin_to_millicelsius(long t)
{

	return deci_kelvin_to_millicelsius_with_offset(t,
	    ABSOLUTE_ZERO_MILLICELSIUS);
}

static inline long
millicelsius_to_deci_kelvin(long t)
{

	return _units_div_round_closest(millicelsius_to_milli_kelvin(t),
	    MILLIDEGREE_PER_DECIDEGREE);
}

static inline long
deci_kelvin_to_celsius(long t)
{

	return _units_div_round_closest(deci_kelvin_to_millicelsius(t),
	    MILLIDEGREE_PER_DEGREE);
}

static inline long
celsius_to_deci_kelvin(long t)
{

	return millicelsius_to_deci_kelvin(t * MILLIDEGREE_PER_DEGREE);
}

static inline long
kelvin_to_celsius(long t)
{

	return _units_div_round_closest(kelvin_to_millicelsius(t),
	    MILLIDEGREE_PER_DEGREE);
}

static inline long
celsius_to_kelvin(long t)
{

	return millicelsius_to_kelvin(t * MILLIDEGREE_PER_DEGREE);
}

#endif	/* _LINUX_UNITS_H_ */
