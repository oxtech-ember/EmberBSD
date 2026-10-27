/* SPDX-License-Identifier: BSD-2-Clause */
/* Run the actual diagnostic function against scripted control events. */
#define open test_open
#define close test_close
#define ioctl test_ioctl
#define main function_main
#include "function.c"
#undef main
#undef ioctl
#undef close
#undef open
#include <assert.h>
#include <stdarg.h>

static unsigned events, transfers;
static const char *scenario;
static unsigned event_index, quiets, attempts;
static bool bus_connected;

static bool
is_case(const char *name)
{

	return strcmp(scenario, name) == 0;
}

int
test_open(const char *path, int flags, ...)
{

	assert(strcmp(path, "fixture") == 0 && flags == O_RDWR);
	return 7;
}

int
test_close(int fd)
{

	assert(fd == 7 && transfers == 1);
	return 0;
}

int
test_ioctl(int fd, unsigned long op, ...)
{
	va_list ap;
	void *arg;

	assert(fd == 7);
	if (op == USBPERIPH_IOC_CONNECT) {
		bus_connected = true;
		return 0;
	}
	if (op == USBPERIPH_IOC_DISCONNECT)
		return 0;
	va_start(ap, op);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (op == USBPERIPH_IOC_CONFIGURE)
		return 0;
	if (op == USBPERIPH_IOC_SNAPSHOT) {
		struct usbperiph_snapshot *s = arg;
		memset(s, 0, sizeof(*s));
		s->version = USBPERIPH_SNAPSHOT_VERSION;
		s->generation = events ? events : bus_connected ? 1 : 0;
		s->configuration = events != 0;
		return 0;
	}
	if (op == USBPERIPH_IOC_GET_EVENT) {
		struct usbperiph_event *e = arg;
		unsigned count = is_case("stable") || is_case("late-cancel") ? 1 :
		    is_case("reset") ? 3 : 2;
		if (event_index++ >= count) {
			/* Catch a non-progressing suspend/disconnect wait. */
			quiets++;
			assert(quiets == 1);
			errno = EWOULDBLOCK;
			return -1;
		}
		memset(e, 0, sizeof(*e));
		e->type = USBPERIPH_EVENT_CONFIGURE;
		e->generation = ++events;
		e->configured = 1;
		if (event_index == 2 && is_case("reset")) {
			e->type = USBPERIPH_EVENT_RESET;
			e->configured = 0;
		} else if (event_index == 2 && is_case("suspend")) {
			e->type = USBPERIPH_EVENT_SUSPEND;
		} else if (event_index == 2 && is_case("disconnect")) {
			e->type = USBPERIPH_EVENT_DISCONNECT;
		} else if (event_index == 1 && is_case("initial-suspend")) {
			e->type = USBPERIPH_EVENT_SUSPEND;
			e->configured = 0;
		} else if (event_index == 1 && is_case("initial-disconnect")) {
			e->type = USBPERIPH_EVENT_DISCONNECT;
			e->configured = 0;
		} else if (event_index == 1 && is_case("stale-disconnect")) {
			e->type = USBPERIPH_EVENT_DISCONNECT;
			e->generation = 0;
			e->configured = 0;
		}
		return 0;
	}
	if (op == USBPERIPH_IOC_XFER) {
		struct usbperiph_xfer *x = arg;
		uint8_t expected[32];
		if (is_case("reconfigure") && events != 2) {
			errno = ECANCELED;
			return -1;
		}
		if (is_case("late-cancel")) {
			attempts++;
			assert(attempts == 1);
			errno = ECANCELED;
			return -1;
		}
		assert(x->endpoint == 0x81 && x->length == sizeof(expected));
		assert(x->generation == events);
		usbperiph_counter(expected, sizeof(expected), 0);
		assert(memcmp((void *)(uintptr_t)x->buffer, expected,
		    sizeof(expected)) == 0);
		x->actual = x->length;
		transfers++;
		return 0;
	}
	assert(!"unexpected ioctl");
	return -1;
}

int
main(int argc, char **argv)
{
	char *args[] = { "function", "fixture", "counter", "0x456",
	    "0xb673", "32", NULL };

	assert(argc == 2);
	scenario = argv[1];
	int result = function_main(6, args);
	assert(result == 0 && transfers == 1);
	return 0;
}
