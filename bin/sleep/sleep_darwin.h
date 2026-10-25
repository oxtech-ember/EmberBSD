/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors (AI-assisted). */

#ifndef SLEEP_DARWIN_H
#define SLEEP_DARWIN_H

#include <limits.h>
#include <stdint.h>
#include <xlocale.h>

static void
sleep_difference(const struct timespec *end, const struct timespec *start,
    struct timespec *result)
{

	result->tv_sec = end->tv_sec - start->tv_sec;
	result->tv_nsec = end->tv_nsec - start->tv_nsec;
	if (result->tv_nsec < 0) {
		result->tv_sec--;
		result->tv_nsec += 1000000000;
	}
}

/* Keep the clock_nanosleep convention: zero or an error number. */
static int
sleep_until(const struct timespec *deadline)
{
	struct timespec now, remaining;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return errno;
	if (deadline->tv_sec < now.tv_sec ||
	    (deadline->tv_sec == now.tv_sec &&
	    deadline->tv_nsec <= now.tv_nsec))
		return 0;
	sleep_difference(deadline, &now, &remaining);
	if (nanosleep(&remaining, NULL) != 0)
		return errno;
	return 0;
}

static int
sleep_deadline(const struct timespec *now, const struct timespec *interval,
    struct timespec *result)
{
	uintmax_t maximum;

	maximum = UINTMAX_MAX >>
	    ((sizeof(uintmax_t) - sizeof(time_t)) * CHAR_BIT);
	if ((time_t)-1 < 0)
		maximum >>= 1;
	if ((uintmax_t)interval->tv_sec > maximum - (uintmax_t)now->tv_sec)
		return EOVERFLOW;
	result->tv_sec = now->tv_sec + interval->tv_sec;
	result->tv_nsec = now->tv_nsec + interval->tv_nsec;
	if (result->tv_nsec >= 1000000000) {
		if (result->tv_sec == (time_t)maximum)
			return EOVERFLOW;
		result->tv_sec++;
		result->tv_nsec -= 1000000000;
	}
	return 0;
}

static double
sleep_c_locale(const char *text, char **end)
{
	locale_t locale;
	double value;
	int saved_errno;

	locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
	if (locale == NULL)
		err(EXIT_FAILURE, "newlocale");
	value = strtod_l(text, end, locale);
	saved_errno = errno;
	if (freelocale(locale) != 0)
		err(EXIT_FAILURE, "freelocale");
	errno = saved_errno;
	return value;
}

#endif /* SLEEP_DARWIN_H */
