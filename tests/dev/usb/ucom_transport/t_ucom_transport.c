/* Origin: EmberBSD real-ucom native software contracts, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/event.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include "umock_test.h"

#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "%s:%d: %s (errno=%d)\n", __func__, __LINE__, #c, errno); \
	exit(1); } } while (0)
#define KERNEL(call) do { rump_schedule(); call; rump_unschedule(); } while (0)

static void
delay(void)
{
	const struct timespec t = { 0, 10000000 };

	(void)nanosleep(&t, NULL);
}

static struct umock_record
snapshot(int p)
{
	struct umock_record r;

	KERNEL(rump_umock_snapshot(p, &r));
	return r;
}

static struct umock_record
pending(int p, unsigned after)
{
	struct umock_record r;

	for (int i = 0; i < 300; i++) {
		r = snapshot(p);
		if (r.pending && r.submits > after)
			return r;
		delay();
	}
	CHECK(0 && "TX not dispatched within 3 seconds");
	return snapshot(p);
}

static int
raw_open(int p)
{
	char path[32];
	int fd = -1;

	snprintf(path, sizeof(path), "/dev/dtyU%d", p);
	/* Rump's direct syscall API exposes kernel restart to its caller. */
	for (int i = 0; i < 300; i++) {
		fd = rump_sys_open(path, O_RDWR | O_NONBLOCK);
		if (fd >= 0 || errno != RUMP_ERESTART)
			return fd;
		delay();
	}
	CHECK(errno != RUMP_ERESTART);
	return fd;
}

static int
open_port(int p)
{
	struct termios t;
	int fd;

	fd = raw_open(p);
	CHECK(fd >= 0);
	CHECK(rump_sys_ioctl(fd, TIOCGETA, &t) == 0);
	cfmakeraw(&t);
	t.c_cflag |= CLOCAL;
	t.c_cflag &= ~HUPCL;
	CHECK(rump_sys_ioctl(fd, TIOCSETA, &t) == 0);
	return fd;
}

static void
outq(int fd, int wanted)
{
	int count = -1;

	for (int i = 0; i < 300; i++) {
		CHECK(rump_sys_ioctl(fd, TIOCOUTQ, &count) == 0);
		if (count == wanted)
			return;
		delay();
	}
	CHECK(count == wanted);
}

static void
read_bytes(int fd, const char *wanted, size_t len)
{
	char data[32];
	ssize_t n = -1;

	CHECK(len <= sizeof(data));
	for (int i = 0; i < 300; i++) {
		n = rump_sys_read(fd, data, len);
		if (n >= 0)
			break;
		CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
		delay();
	}
	CHECK(n == (ssize_t)len && memcmp(data, wanted, len) == 0);
}

static void
complete(int p, struct umock_record r, size_t len, int error)
{

	KERNEL(rump_umock_done(p, r.epoch, r.cookie, len, error));
}

static void
test_open_io(void)
{
	int fd = open_port(0), error;
	unsigned submits = snapshot(0).submits;
	struct umock_record r;

	CHECK(rump_sys_write(fd, "ABC", 3) == 3);
	r = pending(0, submits);
	CHECK(r.length == 3 && memcmp(r.bytes, "ABC", 3) == 0);
	outq(fd, 3);
	complete(0, r, 3, 0);
	outq(fd, 0);
	KERNEL(error = rump_umock_input(0, r.epoch, "XYZ", 3));
	CHECK(error == 0);
	read_bytes(fd, "XYZ", 3);
	CHECK(rump_sys_close(fd) == 0);
}

static void
test_stale_duplicate(void)
{
	int fd = open_port(0), error;
	struct umock_record old, fresh;
	unsigned n = snapshot(0).submits;

	CHECK(rump_sys_write(fd, "old", 3) == 3);
	old = pending(0, n);
	CHECK(rump_sys_close(fd) == 0);
	fd = open_port(0);
	n = snapshot(0).submits;
	CHECK(rump_sys_write(fd, "new", 3) == 3);
	fresh = pending(0, n);
	CHECK(fresh.epoch != old.epoch);
	KERNEL(error = rump_umock_input(0, old.epoch, "bad", 3));
	CHECK(error == ENXIO);
	complete(0, old, 3, 0);
	outq(fd, 3);
	complete(0, fresh, 3, 0);
	outq(fd, 0);
	n = snapshot(0).submits;
	CHECK(rump_sys_write(fd, "NEXT", 4) == 4);
	struct umock_record next = pending(0, n);
	complete(0, fresh, 3, 0);
	outq(fd, 4);
	complete(0, next, 4, 0);
	outq(fd, 0);
	CHECK(rump_sys_close(fd) == 0);
}

static void
test_partial_fault(void)
{
	int fd = open_port(0), other = open_port(1), error;
	char data;
	unsigned n = snapshot(0).submits;

	CHECK(rump_sys_write(fd, "ABC", 3) == 3);
	struct umock_record r = pending(0, n);
	complete(0, r, 2, 0);
	delay();
	CHECK(rump_sys_write(fd, "D", 1) == -1 && errno == EIO);
	CHECK(rump_sys_read(fd, &data, 1) == -1 && errno == EIO);
	CHECK(snapshot(0).submits == n + 1);
	struct umock_record r1 = snapshot(1);
	KERNEL(error = rump_umock_input(1, r1.epoch, "OK", 2));
	CHECK(error == 0);
	read_bytes(other, "OK", 2);
	CHECK(rump_sys_close(fd) == 0 && rump_sys_close(other) == 0);
}

static void
test_pressure_overflow(void)
{
	int fd = open_port(0), other = open_port(1), error;
	uint8_t data[8192];
	char byte;
	struct umock_record r = snapshot(0);

	memset(data, 'A', sizeof(data));
	KERNEL(rump_umock_pause(0, 1));
	for (int i = 0; i < 300 && !snapshot(0).paused; i++)
		delay();
	CHECK(snapshot(0).paused);
	KERNEL(error = rump_umock_input(0, r.epoch, data, 6144));
	CHECK(error == 0);
	KERNEL(error = rump_umock_input(0, r.epoch, data, 2048));
	CHECK(error == 0);
	KERNEL(error = rump_umock_input(0, r.epoch, data, 1));
	CHECK(error == ENOBUFS);
	CHECK(rump_sys_read(fd, &byte, 1) == -1 && errno == ENOBUFS);
	r = snapshot(1);
	KERNEL(error = rump_umock_input(1, r.epoch, "B", 1));
	CHECK(error == 0);
	read_bytes(other, "B", 1);
	CHECK(rump_sys_close(fd) == 0 && rump_sys_close(other) == 0);
	fd = open_port(0);
	r = snapshot(0);
	KERNEL(rump_umock_pause(0, 1));
	KERNEL(error = rump_umock_input(0, r.epoch, "C", 1));
	CHECK(error == 0);
	KERNEL(rump_umock_pause(0, 0));
	read_bytes(fd, "C", 1);
	CHECK(rump_sys_close(fd) == 0);
}

static void
test_flush_pending(void)
{
	int fd = open_port(0), flags = FWRITE;
	unsigned n = snapshot(0).submits;

	CHECK(rump_sys_write(fd, "OLD", 3) == 3);
	struct umock_record old = pending(0, n);
	CHECK(rump_sys_ioctl(fd, TIOCFLUSH, &flags) == 0);
	CHECK(rump_sys_write(fd, "NEW", 3) == 3);
	outq(fd, 3);
	complete(0, old, 3, 0);
	struct umock_record fresh = pending(0, n + 1);
	CHECK(fresh.length == 3 && memcmp(fresh.bytes, "NEW", 3) == 0);
	outq(fd, 3);
	complete(0, fresh, 3, 0);
	outq(fd, 0);
	CHECK(rump_sys_close(fd) == 0);
}

static void
test_rejected_controls(void)
{
	struct termios before, after, t;
	int fd;

	KERNEL(rump_umock_termios(2, &before));
	KERNEL(rump_umock_errors(2, EINVAL, 0, 0));
	CHECK(raw_open(2) == -1 && errno == EINVAL);
	KERNEL(rump_umock_termios(2, &after));
	CHECK(before.c_ospeed == after.c_ospeed && before.c_cflag == after.c_cflag);
	KERNEL(rump_umock_errors(2, 0, EOPNOTSUPP, 0));
	CHECK(raw_open(2) == -1 && errno == EOPNOTSUPP);
	KERNEL(rump_umock_errors(2, 0, 0, EIO));
	CHECK(raw_open(2) == -1 && errno == EIO);
	KERNEL(rump_umock_errors(2, 0, 0, 0));
	fd = open_port(2);
	CHECK(rump_sys_ioctl(fd, TIOCGETA, &before) == 0);
	t = before;
	t.c_ospeed = t.c_ispeed = B19200;
	KERNEL(rump_umock_errors(2, EINVAL, EOPNOTSUPP, 0));
	CHECK(rump_sys_ioctl(fd, TIOCSETA, &t) == -1 && errno == EINVAL);
	CHECK(rump_sys_ioctl(fd, TIOCGETA, &after) == 0);
	CHECK(before.c_ospeed == after.c_ospeed && before.c_cflag == after.c_cflag);
	CHECK(rump_sys_ioctl(fd, TIOCSBRK, NULL) == -1 && errno == EOPNOTSUPP);
	KERNEL(rump_umock_errors(2, 0, 0, 0));
	CHECK(rump_sys_close(fd) == 0);
}

struct close_job { int fd, error; };
struct param_job { int fd, error; struct termios t; };
static void *param_thread(void *arg)
{
	struct param_job *job = arg;

	job->error = rump_sys_ioctl(job->fd, TIOCSETA, &job->t);
	return NULL;
}

static void
test_control_serialization(void)
{
	int fd = open_port(2);
	pthread_t first, second;
	struct param_job a = { .fd = fd }, b = { .fd = fd };
	unsigned before = snapshot(2).param_overlap;

	CHECK(rump_sys_ioctl(fd, TIOCGETA, &a.t) == 0);
	b.t = a.t;
	a.t.c_ispeed = a.t.c_ospeed = B19200;
	b.t.c_ispeed = b.t.c_ospeed = B38400;
	KERNEL(rump_umock_param_gate(2, 1));
	CHECK(pthread_create(&first, NULL, param_thread, &a) == 0);
	for (int i = 0; i < 300 && snapshot(2).param_active == 0; i++)
		delay();
	CHECK(snapshot(2).param_active == 1);
	CHECK(pthread_create(&second, NULL, param_thread, &b) == 0);
	for (int i = 0; i < 20; i++)
		delay();
	CHECK(snapshot(2).param_overlap == (int)before);
	KERNEL(rump_umock_param_gate(2, 0));
	CHECK(pthread_join(first, NULL) == 0 && pthread_join(second, NULL) == 0);
	CHECK(a.error == 0 && b.error == 0);
	CHECK(rump_sys_close(fd) == 0);
}

struct ioctl_job {
	int fd, error, done, flags;
	u_long cmd;
	struct termios t;
};

static void *
ioctl_thread(void *arg)
{
	struct ioctl_job *job = arg;
	void *data = job->cmd == TIOCFLUSH ? (void *)&job->flags : &job->t;

	job->error = rump_sys_ioctl(job->fd, job->cmd, data);
	__atomic_store_n(&job->done, 1, __ATOMIC_RELEASE);
	return NULL;
}

static void
wait_ioctl(struct ioctl_job *job)
{
	for (int i = 0; i < 300; i++) {
		if (__atomic_load_n(&job->done, __ATOMIC_ACQUIRE))
			return;
		delay();
	}
	CHECK(0 && "ioctl did not finish within 3 seconds");
}

static void
test_drain_progress(void)
{
	const u_long commands[] = { TIOCDRAIN, TIOCSETAW, TIOCSETAF };

	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
		for (int flush = 0; flush < 2; flush++) {
			int fd = open_port(0);
			unsigned submits = snapshot(0).submits;
			struct ioctl_job drain = { .fd = fd, .cmd = commands[i] };
			struct ioctl_job resume = { .fd = fd,
			    .cmd = flush ? TIOCFLUSH : TIOCSTART, .flags = FWRITE };
			pthread_t a, b;

			CHECK(rump_sys_ioctl(fd, TIOCGETA, &drain.t) == 0);
			CHECK(rump_sys_ioctl(fd, TIOCSTOP, NULL) == 0);
			CHECK(rump_sys_write(fd, "drain", 5) == 5);
			outq(fd, 5);
			CHECK(pthread_create(&a, NULL, ioctl_thread, &drain) == 0);
			for (int n = 0; n < 10; n++)
				delay();
			CHECK(!__atomic_load_n(&drain.done, __ATOMIC_ACQUIRE));
			CHECK(pthread_create(&b, NULL, ioctl_thread, &resume) == 0);
			wait_ioctl(&resume);
			CHECK(pthread_join(b, NULL) == 0 && resume.error == 0);
			if (!flush) {
				struct umock_record r = pending(0, submits);
				complete(0, r, r.length, 0);
			}
			wait_ioctl(&drain);
			CHECK(pthread_join(a, NULL) == 0 && drain.error == 0);
			CHECK(rump_sys_close(fd) == 0);
		}
	}
}

static void *close_thread(void *arg)
{
	struct close_job *job = arg;

	job->error = rump_sys_close(job->fd);
	return NULL;
}

static void
test_close_dispatch_barrier(void)
{
	int fd = open_port(3);
	struct umock_record before = snapshot(3);
	struct close_job job = { fd, -2 };
	pthread_t thread;

	KERNEL(rump_umock_gate(3, 1));
	CHECK(rump_sys_write(fd, "gate", 4) == 4);
	struct umock_record active = pending(3, before.submits);
	CHECK(active.active);
	CHECK(pthread_create(&thread, NULL, close_thread, &job) == 0);
	for (int i = 0; i < 5; i++)
		delay();
	CHECK(snapshot(3).stops == before.stops);
	KERNEL(rump_umock_gate(3, 0));
	CHECK(pthread_join(thread, NULL) == 0 && job.error == 0);
	CHECK(snapshot(3).stops == before.stops + 1 && !snapshot(3).active);
}

struct read_job { int fd, error; ssize_t result; char byte; };
static void *read_thread(void *arg)
{
	struct read_job *job = arg;

	/* Match libc/kernel restart handling for a canceled blocking read. */
	do {
		job->result = rump_sys_read(job->fd, &job->byte, 1);
	} while (job->result == -1 && errno == RUMP_ERESTART);
	job->error = errno;
	return NULL;
}

static void
test_fault_wakeup_poll(void)
{
	int fd = open_port(0), flags = rump_sys_fcntl(fd, F_GETFL);
	struct read_job job = { .fd = fd };
	struct umock_record r = snapshot(0);
	struct pollfd pollfd = { .fd = fd, .events = POLLIN | POLLOUT };
	pthread_t thread;

	CHECK(flags >= 0);
	CHECK(rump_sys_fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0);
	CHECK(pthread_create(&thread, NULL, read_thread, &job) == 0);
	for (int i = 0; i < 10; i++)
		delay();
	KERNEL(rump_umock_fault(0, r.epoch, ETIMEDOUT));
	CHECK(pthread_join(thread, NULL) == 0);
	CHECK(job.result == -1 && (job.error == EIO || job.error == ETIMEDOUT));
	CHECK(rump_sys_poll(&pollfd, 1, 0) == 1 && (pollfd.revents & POLLERR));
	CHECK(raw_open(0) == -1 && errno == ETIMEDOUT);
	CHECK(rump_sys_close(fd) == 0);
}

struct poll_job { int fd, result; short revents; };
static void *
poll_thread(void *arg)
{
	struct poll_job *job = arg;
	struct pollfd p = { .fd = job->fd, .events = POLLIN };

	job->result = rump_sys_poll(&p, 1, 3000);
	job->revents = p.revents;
	return NULL;
}

static void
test_fault_blocked_poll(void)
{
	/* Include faults racing with registration as well as an established wait. */
	for (int i = 0; i < 24; i++) {
		int fd = open_port(0);
		struct umock_record r = snapshot(0);
		struct poll_job job = { .fd = fd };
		pthread_t thread;

		CHECK(pthread_create(&thread, NULL, poll_thread, &job) == 0);
		if (i == 0)
			for (int n = 0; n < 10; n++)
				delay();
		KERNEL(rump_umock_fault(0, r.epoch, ETIMEDOUT));
		CHECK(pthread_join(thread, NULL) == 0);
		CHECK(job.result == 1 && (job.revents & (POLLERR | POLLHUP)));
		CHECK(rump_sys_close(fd) == 0);
	}
}

struct event_job { int queue, result; struct kevent event; };
static void *
event_thread(void *arg)
{
	struct event_job *job = arg;
	const struct timespec timeout = { 3, 0 };

	job->result = rump_sys_kevent(job->queue, NULL, 0, &job->event, 1, &timeout);
	return NULL;
}

static void
test_fault_kqueue(void)
{
	int fd = open_port(0), queue = rump_sys_kqueue();
	struct umock_record r = snapshot(0);
	struct kevent change, event = { 0 };
	struct event_job job = { .queue = queue };
	const struct timespec zero = { 0, 0 };
	pthread_t thread;

	CHECK(queue >= 0);
	EV_SET(&change, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
	CHECK(rump_sys_kevent(queue, &change, 1, &event, 1, &zero) == 0);
	int error;
	KERNEL(error = rump_umock_input(0, r.epoch, "KQ", 2));
	CHECK(error == 0);
	for (int n = 0; n < 300; n++) {
		if (rump_sys_kevent(queue, NULL, 0, &event, 1, &zero) == 1)
			break;
		delay();
	}
	CHECK(event.filter == EVFILT_READ && event.data == 2 && !(event.flags & EV_EOF));
	read_bytes(fd, "KQ", 2);
	CHECK(pthread_create(&thread, NULL, event_thread, &job) == 0);
	for (int n = 0; n < 10; n++)
		delay();
	KERNEL(rump_umock_fault(0, r.epoch, ETIMEDOUT));
	CHECK(pthread_join(thread, NULL) == 0);
	CHECK(job.result == 1 && job.event.filter == EVFILT_READ);
	CHECK((job.event.flags & EV_EOF) && job.event.fflags == ETIMEDOUT);
	EV_SET(&change, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	CHECK(rump_sys_kevent(queue, &change, 1, NULL, 0, NULL) == 0);
	EV_SET(&change, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
	CHECK(rump_sys_kevent(queue, &change, 1, &event, 1, &zero) == 1);
	CHECK((event.flags & EV_EOF) && event.fflags == ETIMEDOUT);
	CHECK(rump_sys_close(queue) == 0 && rump_sys_close(fd) == 0);
}

static struct umock_mux_frame
mux_pick(uint64_t now)
{
	struct umock_mux_frame f;
	int error;

	KERNEL(error = rump_umock_mux_pick(now, &f));
	CHECK(error == 0);
	return f;
}

static void
mux_done(struct umock_mux_frame f)
{

	KERNEL(rump_umock_mux_done(f.token, f.length, 0));
}

static void
test_shared_round_robin(void)
{
	int fd[4], error;
	struct umock_mux_frame f, blocked;
	uint64_t old = 0;

	KERNEL(error = rump_umock_mux_enable(1));
	CHECK(error == 0);
	for (int p = 0; p < 4; p++) {
		fd[p] = open_port(p);
		unsigned n = snapshot(p).submits;
		CHECK(rump_sys_write(fd[p], "mux", 3) == 3);
		(void)pending(p, n);
	}
	for (int p = 0; p < 4; p++) {
		f = mux_pick(p + 1);
		CHECK(f.port == (unsigned)p && f.length == 7);
		CHECK(f.bytes[0] == 0 && f.bytes[1] == p);
		CHECK(memcmp(f.bytes + 4, "mux", 3) == 0);
		if (old != 0)
			KERNEL(rump_umock_mux_done(old, 7, EIO));
		KERNEL(error = rump_umock_mux_pick(p + 1, &blocked));
		CHECK(error == EBUSY);
		outq(fd[p], 3);
		old = f.token;
		mux_done(f);
		outq(fd[p], 0);
	}
	for (int p = 0; p < 4; p++)
		CHECK(rump_sys_close(fd[p]) == 0);
	KERNEL(error = rump_umock_mux_enable(0));
	CHECK(error == 0);
}

static void
test_shared_credit_timeout(void)
{
	char data[600], byte;
	int fd[2], error;
	struct umock_mux_frame f;
	struct pollfd pollfd;

	memset(data, 'c', sizeof(data));
	KERNEL(error = rump_umock_mux_enable(1));
	CHECK(error == 0);
	fd[0] = open_port(0);
	fd[1] = open_port(1);
	unsigned n = snapshot(0).submits;
	CHECK(rump_sys_write(fd[0], data, sizeof(data)) == sizeof(data));
	(void)pending(0, n);
	f = mux_pick(1);
	CHECK(f.port == 0 && f.length == 604 && f.bytes[0] == 0);
	mux_done(f);
	outq(fd[0], 0);
	n = snapshot(0).submits;
	CHECK(rump_sys_write(fd[0], "s", 1) == 1);
	(void)pending(0, n);
	f = mux_pick(2);
	CHECK(f.bytes[0] == 0x80 && f.port == 0);
	mux_done(f);
	outq(fd[0], 0);
	n = snapshot(0).submits;
	CHECK(rump_sys_write(fd[0], "wait", 4) == 4);
	(void)pending(0, n);
	n = snapshot(1).submits;
	CHECK(rump_sys_write(fd[1], "peer", 4) == 4);
	(void)pending(1, n);
	f = mux_pick(3);
	CHECK(f.port == 1);
	mux_done(f);
	outq(fd[1], 0);
	KERNEL(error = rump_umock_mux_tick(102));
	CHECK(error == 0);
	CHECK(rump_sys_read(fd[0], &byte, 1) == -1 && errno == ETIMEDOUT);
	pollfd = (struct pollfd){ .fd = fd[0], .events = POLLIN | POLLOUT };
	CHECK(rump_sys_poll(&pollfd, 1, 0) == 1 && (pollfd.revents & POLLERR));
	n = snapshot(1).submits;
	CHECK(rump_sys_write(fd[1], "ok", 2) == 2);
	(void)pending(1, n);
	f = mux_pick(103);
	CHECK(f.port == 1);
	mux_done(f);
	outq(fd[1], 0);
	CHECK(rump_sys_close(fd[0]) == 0 && rump_sys_close(fd[1]) == 0);
	KERNEL(error = rump_umock_mux_enable(0));
	CHECK(error == 0);
}

static void
test_shared_close_bad_event(void)
{
	int fd[2], error;
	struct umock_mux_frame old, fresh, blocked;
	uint8_t bad[16] = { 0, 0, 0, 2, 0, 0, 0, 0, 0, 4, 0, 2 };
	char byte;

	KERNEL(error = rump_umock_mux_enable(1));
	CHECK(error == 0);
	fd[0] = open_port(0);
	unsigned n = snapshot(0).submits;
	CHECK(rump_sys_write(fd[0], "old", 3) == 3);
	(void)pending(0, n);
	old = mux_pick(1);
	CHECK(rump_sys_close(fd[0]) == 0);
	fd[0] = open_port(0);
	fd[1] = open_port(1);
	n = snapshot(0).submits;
	CHECK(rump_sys_write(fd[0], "fresh", 5) == 5);
	(void)pending(0, n);
	fresh = mux_pick(2);
	CHECK(fresh.epoch != old.epoch && fresh.token != old.token);
	KERNEL(rump_umock_mux_done(old.token, old.length, 0));
	outq(fd[0], 5);
	KERNEL(error = rump_umock_mux_pick(2, &blocked));
	CHECK(error == EBUSY);
	mux_done(fresh);
	outq(fd[0], 0);
	KERNEL(error = rump_umock_mux_events(bad, sizeof(bad)));
	CHECK(error == EPROTO);
	for (int p = 0; p < 2; p++) {
		CHECK(rump_sys_read(fd[p], &byte, 1) == -1 && errno == EPROTO);
		CHECK(rump_sys_close(fd[p]) == 0);
	}
	KERNEL(error = rump_umock_mux_enable(0));
	CHECK(error == 0);
}

static void
test_detach_failed_attach_legacy(void)
{
	int fd = open_port(3), error;
	struct umock_record before = snapshot(3);
	char byte;
	unsigned usb;

	KERNEL(error = rump_umock_remove(3));
	CHECK(error == 0);
	CHECK(snapshot(3).detaches == before.detaches + 1);
	/* Revoked TTY vnode reads may report EOF; no data may be delivered. */
	CHECK(rump_sys_read(fd, &byte, 1) <= 0);
	CHECK(rump_sys_write(fd, "X", 1) == -1);
	CHECK(rump_sys_close(fd) == 0);
	CHECK(rump_sys_open("/dev/dtyU3", O_RDWR | O_NONBLOCK) == -1);
	CHECK(rump_sys_open("/dev/dtyU4", O_RDWR | O_NONBLOCK) == -1 && errno == ENXIO);
	CHECK(rump_sys_open("/dev/dtyU5", O_RDWR | O_NONBLOCK) == -1 && errno == ENXIO);
	CHECK(snapshot(5).detaches == 0);
	CHECK(rump_sys_open("/dev/dtyU6", O_RDWR | O_NONBLOCK) == -1 && errno == ENXIO);
	KERNEL(usb = rump_umock_usb_calls());
	CHECK(usb == 1);
}

int
main(int argc, char **argv)
{
	unsigned ran = 0;
	struct { const char *name; void (*test)(void); } tests[] = {
		{ "open_io", test_open_io },
		{ "stale_duplicate", test_stale_duplicate },
		{ "partial_fault", test_partial_fault },
		{ "pressure_overflow", test_pressure_overflow },
		{ "flush_pending", test_flush_pending },
		{ "rejected_controls", test_rejected_controls },
		{ "control_serialization", test_control_serialization },
		{ "drain_progress", test_drain_progress },
		{ "close_dispatch_barrier", test_close_dispatch_barrier },
		{ "fault_wakeup_poll", test_fault_wakeup_poll },
		{ "fault_blocked_poll", test_fault_blocked_poll },
		{ "fault_kqueue", test_fault_kqueue },
		{ "shared_round_robin", test_shared_round_robin },
		{ "shared_credit_timeout", test_shared_credit_timeout },
		{ "shared_close_bad_event", test_shared_close_bad_event },
		{ "detach_failed_attach_legacy", test_detach_failed_attach_legacy }
	};

	alarm(45);
	CHECK(argc <= 2);
	CHECK(rump_init() == 0);
	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		if (argc > 1 && strcmp(argv[1], tests[i].name) != 0)
			continue;
		tests[i].test();
		ran++;
		printf("PASS %s\n", tests[i].name);
		fflush(stdout);
	}
	CHECK(ran != 0);
	printf("ucom native: %u/%u groups passed, 0 failures (test-only USB)\n", ran, ran);
	return 0;
}
