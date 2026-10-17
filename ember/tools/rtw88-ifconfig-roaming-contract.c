/* SPDX-License-Identifier: BSD-2-Clause */
/* Original native ifconfig roaming contract, prepared with AI assistance. */
#include <sys/types.h>
#include <net/if.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <net80211/_ieee80211.h>
#include "native-ioccom.h"
#include "roaming-request.inc"
#include "native-simpleq.h"

/* Property storage and the final syscall are the host test boundaries. */
struct property {
	const char *key;
	struct property *value;
	int64_t number;
};
typedef struct property *prop_object_t;
typedef struct property *prop_dictionary_t;
static struct property properties[32];
static unsigned nproperties;
static prop_object_t
property_new(void)
{
	assert(nproperties < sizeof(properties) / sizeof(properties[0]));
	return &properties[nproperties++];
}
static prop_dictionary_t prop_dictionary_create(void) { return property_new(); }
static prop_dictionary_t
prop_dictionary_copy(prop_dictionary_t d)
{
	prop_dictionary_t copy = property_new();
	*copy = *d;
	return copy;
}
static prop_dictionary_t
prop_dictionary_augment(prop_dictionary_t d, prop_dictionary_t extra)
{
	assert(extra == NULL);
	return prop_dictionary_copy(d);
}
static bool
prop_dictionary_set(prop_dictionary_t d, const char *key, prop_object_t o)
{
	d->key = key;
	d->value = o;
	return true;
}
static prop_object_t
prop_dictionary_get(prop_dictionary_t d, const char *key)
{
	return d != NULL && d->key != NULL && strcmp(key, d->key) == 0 ?
	    d->value : NULL;
}
static bool
prop_dictionary_get_int16(prop_dictionary_t d, const char *key, int16_t *v)
{
	prop_object_t o = prop_dictionary_get(d, key);
	if (o == NULL || o->number < INT16_MIN || o->number > INT16_MAX)
		return false;
	*v = (int16_t)o->number;
	return true;
}
static prop_object_t
prop_number_create_signed(int64_t v)
{
	prop_object_t o = property_new();
	o->number = v;
	return o;
}
static prop_object_t prop_number_create_unsigned(uint64_t v)
{ return prop_number_create_signed((int64_t)v); }
static prop_object_t prop_bool_create(bool v)
{ return prop_number_create_signed(v); }
static prop_object_t prop_string_create_nocopy(const char *v)
{ (void)v; abort(); }
static void prop_object_release(prop_object_t o) { (void)o; }

#include "roaming-parser-types.inc"
static int pkw_match(const struct parser *, const struct match *,
    struct match *, int, const char *);
const struct parser_methods pkw_methods = { .pm_match = pkw_match };
struct pbranch command_root;
struct pstr parse_bssid, parse_ssid, parse_nwkey;
struct pinteger parse_chan, parse_frag, parse_rts, parse_powersavesleep;
static struct pkw lists;
static int
other_command(prop_dictionary_t env, prop_dictionary_t oenv)
{
	(void)env;
	(void)oenv;
	abort();
}
#define unsetifbssid other_command
#define setifchan other_command
#define setiffrag other_command
#define unsetifnwkey other_command
#define setifrts other_command
#define dbg_warnx(...) ((void)0)

static struct ieee80211req captured;
static unsigned ioctl_calls, warnings;
static unsigned long captured_command;
static int ioctl_error;
static int getsock(int family) { (void)family; return 7; }
static const char *getifname(prop_dictionary_t env)
{ (void)env; return "rtwtest0"; }
static void
estrlcpy(void *dest, const char *src, size_t size)
{
	assert(strlen(src) < size);
	memcpy(dest, src, strlen(src) + 1);
}
static int
prog_ioctl(int s, unsigned long command, void *request)
{
	assert(s == 7);
	captured_command = command;
	memcpy(&captured, request, sizeof(captured));
	ioctl_calls++;
	if (ioctl_error != 0) {
		errno = ioctl_error;
		return -1;
	}
	return 0;
}
static void warn(const char *format, ...) { (void)format; warnings++; }
static void err(int code, const char *format, ...)
{ (void)code; (void)format; abort(); }
static void errx(int code, const char *format, ...)
{ (void)code; (void)format; abort(); }
#include "roaming-parser.inc"
#include "roaming-cli.inc"

static unsigned checks, failures;
#define CHECK(e) do { checks++; if (!(e)) { failures++; \
	fprintf(stderr, "line %u: %s\n", (unsigned)__LINE__, #e); } } while (0)

static struct pkw *
roaming_parser(void)
{
	struct kwinst *k;
	SIMPLEQ_FOREACH(k, &kw80211.pk_keywords, k_next) {
		if (strcmp(k->k_word, "roaming") == 0)
			return (struct pkw *)k->k_nextparser;
	}
	return NULL;
}

static int
apply(const char *mode)
{
	struct match matches[2] = {{0}};
	struct match initial = {0};
	const struct parser *next;
	int error;

	nproperties = 0;
	memset(properties, 0, sizeof(properties));
	initial.m_env = prop_dictionary_create();
	error = pkw_match(&kw80211.pk_parser, &initial, &matches[0], 0, "roaming");
	if (error != 0)
		return error;
	next = matches[0].m_nextparser;
	if (next == NULL)
		return -1;
	error = next->p_methods->pm_match(next, &matches[0], &matches[1], 1, mode);
	if (error != 0)
		return error;
	CHECK(matches[1].m_nextparser == &command_root.pb_parser);
	return matches_exec(matches, NULL, 2);
}

static void
clear_keywords(struct pkw *p)
{
	struct kwinst *k;
	while ((k = SIMPLEQ_FIRST(&p->pk_keywords)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&p->pk_keywords, k_next);
		free(k);
	}
}

int
main(void)
{
	const char *const modes[] = { "device", "auto", "manual" };
	const int errors[] = { EIO, ENXIO, EOPNOTSUPP, EPERM };
	struct pkw *child;
	struct ieee80211req expected;
	unsigned i, j, calls_before;
	(void)set80211;
	(void)prop_dictionary_get_int16;

	CHECK(pkw_setwords(&kw80211, kw80211.pk_execinit, kw80211.pk_keyinit,
	    kw80211.pk_kwinit, kw80211.pk_nkwinit, kw80211.pk_nextinit) == 0);
	child = roaming_parser();
	if (child != NULL)
		CHECK(pkw_setwords(child, child->pk_execinit, child->pk_keyinit,
		    child->pk_kwinit, child->pk_nkwinit, child->pk_nextinit) == 0);
	/* A missing registration or wrong value must fail at the ioctl consumer. */
	for (i = 0; i < 3; i++) {
		memset(&captured, 0x5a, sizeof(captured));
		ioctl_calls = warnings = 0;
		ioctl_error = 0;
		CHECK(apply(modes[i]) == 0);
		CHECK(ioctl_calls == 1 && warnings == 0);
		CHECK(captured_command == (unsigned long)0x802069f4);
		memset(&expected, 0, sizeof(expected));
		memcpy(expected.i_name, "rtwtest0", 9);
		expected.i_type = 16;
		expected.i_val = (int16_t)i;
		CHECK(memcmp(&captured, &expected, sizeof(expected)) == 0);
		/* Kernel failures must remain failures, not successful startup. */
		for (j = 0; j < sizeof(errors) / sizeof(errors[0]); j++) {
			ioctl_error = errors[j];
			ioctl_calls = warnings = 0;
			errno = 0;
			CHECK(apply(modes[i]) == -1);
			CHECK(errno == errors[j] && ioctl_calls == 1 && warnings == 1);
		}
	}
	calls_before = ioctl_calls;
	CHECK(apply(NULL) == -1);
	CHECK(apply("") == -1);
	CHECK(apply("MANUAL") == -1);
	CHECK(apply("2") == -1);
	CHECK(apply("unknown") == -1);
	CHECK(ioctl_calls == calls_before);
	if (child != NULL)
		clear_keywords(child);
	clear_keywords(&kw80211);
	printf("ifconfig roaming: %u assertions, %u failures\n", checks, failures);
	return failures != 0;
}
