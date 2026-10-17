/* Origin: EmberBSD external-ucom native test interface, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _UMOCK_TEST_H_
#define _UMOCK_TEST_H_
#include <sys/types.h>
#include <sys/termios.h>

struct umock_record {
	uint64_t epoch, cookie;
	size_t length;
	unsigned starts, stops, detaches, submits;
	int pending, paused, param_error, set_error, start_error;
	int gate, active;
	int param_gate, param_active, param_overlap;
	uint8_t bytes[32];
};

struct umock_mux_frame {
	uint64_t token, epoch, cookie;
	size_t length;
	unsigned int port;
	uint8_t bytes[1024];
};
int rump_umock_mux_enable(int);
int rump_umock_mux_pick(uint64_t, struct umock_mux_frame *);
void rump_umock_mux_done(uint64_t, size_t, int);
int rump_umock_mux_events(const uint8_t *, size_t);
int rump_umock_mux_tick(uint64_t);

/* Call only between rump_schedule()/rump_unschedule(). */
void rump_umock_snapshot(int, struct umock_record *);
void rump_umock_errors(int, int, int, int);
int rump_umock_input(int, uint64_t, const void *, size_t);
void rump_umock_done(int, uint64_t, uint64_t, size_t, int);
void rump_umock_fault(int, uint64_t, int);
void rump_umock_pause(int, int);
void rump_umock_gate(int, int);
int rump_umock_remove(int);
unsigned rump_umock_usb_calls(void);
void rump_umock_termios(int, struct termios *);
void rump_umock_param_gate(int, int);
#endif
