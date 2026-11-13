/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Client for the vgem test driver (sys/modules/vgem): exercises dma_fence,
 * dma_resv and dma-buf through a real DRM driver.  Dumb buffers, PRIME export
 * and import, DRM_IOCTL_VGEM_FENCE_ATTACH (with and without the WRITE flag),
 * DRM_IOCTL_VGEM_FENCE_SIGNAL, poll(2) and kevent(2) on the dma-buf, fence
 * expiry, close with pending fences, and a concurrent stress.
 *
 * The output is one "ok"/"FAIL" line per check, no timings in the line, so
 * the output of different kernels can be compared with diff(1).
 * usage: drm2-vgem-client [-s stress_iterations] [-q]    (-q: skip the 10 s expiry check)
 * exit status 0 if every check passed.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/event.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/vgem_drm.h>

static int fd = -1;
static int nchecks, nfail;

#define CHECK(cond, ...) do { \
	nchecks++; \
	if (cond) { printf("ok   "); } else { printf("FAIL "); nfail++; } \
	printf(__VA_ARGS__); printf("\n"); fflush(stdout); \
} while (0)

static int
ioc(unsigned long req, void *arg)
{
	return ioctl(fd, req, arg) < 0 ? -errno : 0;
}

static double
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int
open_vgem(void)
{
	char path[64], name[32];
	int i, f;
	struct drm_version v;

	/* CREATE_DUMB is not allowed on render nodes: use the primary node. */
	for (i = 0; i < 64; i++) {
		snprintf(path, sizeof path, "/dev/dri/card%d", i);
		f = open(path, O_RDWR | O_CLOEXEC);
		if (f < 0)
			continue;
		memset(&v, 0, sizeof v);
		memset(name, 0, sizeof name);
		v.name = name;
		v.name_len = sizeof name - 1;
		if (ioctl(f, DRM_IOCTL_VERSION, &v) == 0 &&
		    strcmp(name, "vgem") == 0)
			return f;
		close(f);
	}
	return -1;
}

static int
mkdumb(uint32_t w, uint32_t h, uint32_t bpp, uint32_t *handle, uint64_t *size)
{
	struct drm_mode_create_dumb cd;
	int r;

	memset(&cd, 0, sizeof cd);
	cd.width = w; cd.height = h; cd.bpp = bpp;
	r = ioc(DRM_IOCTL_MODE_CREATE_DUMB, &cd);
	if (r == 0) {
		*handle = cd.handle;
		if (size)
			*size = cd.size;
	}
	return r;
}

static int
export_fd(uint32_t handle, int *out)
{
	struct drm_prime_handle ph;
	int r;

	memset(&ph, 0, sizeof ph);
	ph.handle = handle; ph.flags = O_CLOEXEC; ph.fd = -1;
	r = ioc(DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph);
	if (r == 0)
		*out = ph.fd;
	return r;
}

static int
attach(int f, uint32_t handle, uint32_t flags, uint32_t pad, uint32_t *id)
{
	struct drm_vgem_fence_attach a;
	int r;

	memset(&a, 0, sizeof a);
	a.handle = handle; a.flags = flags; a.pad = pad;
	r = ioctl(f, DRM_IOCTL_VGEM_FENCE_ATTACH, &a) < 0 ? -errno : 0;
	if (r == 0 && id)
		*id = a.out_fence;
	return r;
}

static int
fsignal(int f, uint32_t id, uint32_t flags)
{
	struct drm_vgem_fence_signal s;

	memset(&s, 0, sizeof s);
	s.fence = id; s.flags = flags;
	return ioctl(f, DRM_IOCTL_VGEM_FENCE_SIGNAL, &s) < 0 ? -errno : 0;
}

/* poll(2) result: revents masked to ev, or -errno; timeout in ms. */
static int
pollev(int pfd, short ev, int ms)
{
	struct pollfd p;
	int r;

	p.fd = pfd; p.events = ev; p.revents = 0;
	r = poll(&p, 1, ms);
	if (r < 0)
		return -errno;
	if (r == 0)
		return 0;
	return p.revents & (ev | POLLERR | POLLHUP | POLLNVAL);
}

static int __attribute__((unused))
kev_ready(int kq, int pfd, int ms)
{
	struct kevent ke, out;
	struct timespec ts;
	int r;

	EV_SET(&ke, pfd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, 0);
	ts.tv_sec = ms / 1000; ts.tv_nsec = (ms % 1000) * 1000000L;
	r = kevent(kq, &ke, 1, &out, 1, &ts);
	if (r < 0)
		return -errno;
	return r;
}

#define IN	POLLIN
#define OUT	POLLOUT

static void
test_basic(uint32_t *ha, uint32_t *hb, int *fa, int *fb)
{
	uint64_t size = 0;
	uint32_t h2 = 0;
	int r, imp;
	struct drm_prime_handle ph;

	r = mkdumb(64, 64, 32, ha, &size);
	if (r) fprintf(stderr, "note: create_dumb errno %d\n", -r);
	CHECK(r == 0 && size == 16384,
	    "dumb 64x64x32 creates a 16384-byte object");
	CHECK(mkdumb(32, 32, 32, hb, NULL) == 0, "second dumb buffer");
	CHECK(mkdumb(64, 64, 16, &h2, NULL) == -EINVAL, "dumb with bpp 16 is EINVAL");
	CHECK(mkdumb(0, 64, 32, &h2, NULL) == -EINVAL, "dumb with width 0 is EINVAL");
	r = export_fd(*ha, fa);
	CHECK(r == 0 && *fa >= 0, "PRIME export of buffer A");
	r = export_fd(*hb, fb);
	CHECK(r == 0 && *fb >= 0, "PRIME export of buffer B");
	r = export_fd(0xdead, &imp);
	CHECK(r == -ENOENT || r == -EINVAL, "PRIME export of a bad handle fails (%s)",
	    r == -ENOENT ? "ENOENT" : r == -EINVAL ? "EINVAL" : "other");
	memset(&ph, 0, sizeof ph);
	ph.fd = *fa;
	r = ioc(DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph);
	CHECK(r == 0 && ph.handle == *ha, "PRIME import of A gives the same handle");
	r = pollev(*fa, IN | OUT, 0);
	CHECK(r == (IN | OUT), "idle dma-buf A polls ready for IN and OUT");
}

static void
test_args(uint32_t ha)
{
	uint32_t id = 0;

	CHECK(attach(fd, ha, 2, 0, &id) == -EINVAL, "attach with unknown flag is EINVAL");
	CHECK(attach(fd, ha, 0, 1, &id) == -EINVAL, "attach with pad set is EINVAL");
	CHECK(attach(fd, 0xdead, 0, 0, &id) == -ENOENT, "attach to a bad handle is ENOENT");
	CHECK(fsignal(fd, 12345, 0) == -ENOENT, "signal of an unknown fence is ENOENT");
	CHECK(fsignal(fd, 1, 1) == -EINVAL, "signal with flags is EINVAL");
}

static void
test_write_fence(uint32_t ha, uint32_t hb, int fa, int fb)
{
	uint32_t id = 0, id2 = 0;
	int r;

	r = attach(fd, ha, VGEM_FENCE_WRITE, 0, &id);
	CHECK(r == 0 && id != 0, "attach WRITE fence to A");
	CHECK(pollev(fa, IN, 50) == 0, "WRITE pending: POLLIN is not ready");
	CHECK(pollev(fa, OUT, 50) == 0, "WRITE pending: POLLOUT is not ready");
	CHECK(pollev(fb, IN | OUT, 0) == (IN | OUT), "buffer B is not affected by the fence on A");
	CHECK(attach(fd, ha, 0, 0, &id2) == -EBUSY, "READ attach while a WRITE is pending is EBUSY");
	CHECK(attach(fd, ha, VGEM_FENCE_WRITE, 0, &id2) == -EBUSY, "WRITE attach while a WRITE is pending is EBUSY");
	CHECK(fsignal(fd, id, 0) == 0, "signal the WRITE fence");
	CHECK(pollev(fa, IN | OUT, 0) == (IN | OUT), "after signal both IN and OUT are ready");
	CHECK(fsignal(fd, id, 0) == -ENOENT, "second signal of the same fence is ENOENT");
	(void)hb;
}

static void
test_read_fence(uint32_t ha, int fa)
{
	uint32_t id = 0, id2 = 0, id3 = 0;
	int r;

	r = attach(fd, ha, 0, 0, &id);
	CHECK(r == 0 && id != 0, "attach READ fence to A");
	CHECK(pollev(fa, IN, 0) == IN, "READ pending: POLLIN is ready");
	CHECK(pollev(fa, OUT, 50) == 0, "READ pending: POLLOUT is not ready");
	CHECK(attach(fd, ha, VGEM_FENCE_WRITE, 0, &id3) == -EBUSY, "WRITE attach while a READ is pending is EBUSY");
	r = attach(fd, ha, 0, 0, &id2);
	CHECK(r == 0 && id2 != 0 && id2 != id, "second READ fence is accepted");
	CHECK(fsignal(fd, id, 0) == 0, "signal the first READ fence");
	CHECK(pollev(fa, OUT, 50) == 0, "one READ still pending: POLLOUT is not ready");
	CHECK(fsignal(fd, id2, 0) == 0, "signal the second READ fence");
	CHECK(pollev(fa, IN | OUT, 0) == (IN | OUT), "all READ fences signalled: IN and OUT ready");
	/* a writer may follow the readers */
	r = attach(fd, ha, VGEM_FENCE_WRITE, 0, &id3);
	CHECK(r == 0, "WRITE attach after the readers finished");
	CHECK(fsignal(fd, id3, 0) == 0, "signal that WRITE fence");
}

/*
 * kqueue on a dma-buf is not implemented in the layer (dma_resv_kqfilter
 * returns EINVAL, as in NetBSD); check that userland sees that error and not
 * a false readiness event.
 */
static void
test_kqueue(uint32_t ha, int fa)
{
	struct kevent ke, out;
	struct timespec ts = { 0, 50 * 1000000L };
	uint32_t id = 0;
	int kq, r;

	kq = kqueue();
	CHECK(kq >= 0, "kqueue created");
	if (kq < 0)
		return;
	CHECK(attach(fd, ha, VGEM_FENCE_WRITE, 0, &id) == 0, "attach WRITE fence for kevent");
	EV_SET(&ke, fa, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, 0);
	memset(&out, 0, sizeof out);
	r = kevent(kq, &ke, 1, &out, 1, &ts);
	CHECK(r == 1 && (out.flags & EV_ERROR) && out.data == EINVAL,
	    "kevent on the dma-buf reports EINVAL (kqfilter is not implemented)");
	CHECK(fsignal(fd, id, 0) == 0, "signal the fence");
	close(kq);
}

static void
test_wakeup(uint32_t ha, int fa)
{
	uint32_t id = 0;
	pid_t pid;
	double t0, dt;
	int r, st;

	CHECK(attach(fd, ha, VGEM_FENCE_WRITE, 0, &id) == 0, "attach WRITE fence for the wake-up test");
	pid = fork();
	if (pid == 0) {
		usleep(200 * 1000);
		_exit(fsignal(fd, id, 0) == 0 ? 0 : 1);
	}
	t0 = now_ms();
	r = pollev(fa, IN, 3000);
	dt = now_ms() - t0;
	waitpid(pid, &st, 0);
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "child signalled the fence through the shared fd");
	CHECK(r == IN, "blocking poll wakes up on the signal");
	CHECK(dt > 100 && dt < 1500, "the wake-up took between 100 and 1500 ms");
	fprintf(stderr, "note: wake-up after %.0f ms\n", dt);
}

static void
test_expiry(uint32_t ha, int fa)
{
	uint32_t id = 0;
	double t0, dt;
	int r;

	CHECK(attach(fd, ha, VGEM_FENCE_WRITE, 0, &id) == 0, "attach WRITE fence that nobody signals");
	t0 = now_ms();
	r = pollev(fa, IN, 15000);
	dt = now_ms() - t0;
	CHECK(r == IN, "poll returns when the 10 s fence expires on its own");
	CHECK(dt > 9000 && dt < 13000, "the expiry took between 9 and 13 s");
	fprintf(stderr, "note: expiry after %.0f ms\n", dt);
	CHECK(fsignal(fd, id, 0) == -ETIMEDOUT, "signal of an expired fence is ETIMEDOUT");
}

static void
test_close_pending(void)
{
	int f2, dfd = -1, save;
	uint32_t h = 0, id = 0;

	f2 = open_vgem();
	CHECK(f2 >= 0, "second open of the primary node");
	if (f2 < 0)
		return;
	save = fd; fd = f2;
	CHECK(mkdumb(16, 16, 32, &h, NULL) == 0, "dumb buffer on the second open");
	fd = save;
	{
		struct drm_prime_handle ph;
		memset(&ph, 0, sizeof ph);
		ph.handle = h; ph.flags = O_CLOEXEC; ph.fd = -1;
		CHECK(ioctl(f2, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph) == 0, "export on the second open");
		dfd = ph.fd;
	}
	CHECK(attach(f2, h, VGEM_FENCE_WRITE, 0, &id) == 0, "attach WRITE fence on the second open");
	CHECK(dfd >= 0 && pollev(dfd, IN, 0) == 0, "fence pending before the close");
	close(f2);
	CHECK(dfd >= 0 && pollev(dfd, IN | OUT, 1000) == (IN | OUT),
	    "closing the DRM fd signals pending fences; the dma-buf outlives the handle");
	if (dfd >= 0)
		close(dfd);
}

static volatile sig_atomic_t stop;
static void onterm(int s) { (void)s; stop = 1; }

static void
test_stress(uint32_t ha, int fa, int iters)
{
	pid_t pids[3];
	int i, np = 0, st, bad = 0, mismatch = 0;
	uint32_t id;
	int r;
	unsigned long polls_unexpected = 0;

	for (i = 0; i < 3; i++) {
		pid_t pid = fork();
		if (pid == 0) {
			/* poller: hammer the dma-buf, only errors matter */
			unsigned long err = 0;
			signal(SIGTERM, onterm);
			while (!stop) {
				int q = pollev(fa, IN | OUT, 1);
				if (q < 0 || (q & (POLLERR | POLLHUP | POLLNVAL)))
					err++;
			}
			_exit(err ? 1 : 0);
		}
		if (pid > 0)
			pids[np++] = pid;
	}
	for (i = 0; i < iters; i++) {
		int write = i & 1;
		r = attach(fd, ha, write ? VGEM_FENCE_WRITE : 0, 0, &id);
		if (r != 0) { bad++; continue; }
		if (pollev(fa, write ? IN : OUT, 0) != 0) mismatch++;
		if (fsignal(fd, id, 0) != 0) bad++;
		if (pollev(fa, IN | OUT, 0) != (IN | OUT)) mismatch++;
	}
	for (i = 0; i < np; i++)
		kill(pids[i], SIGTERM);
	for (i = 0; i < np; i++) {
		waitpid(pids[i], &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
			polls_unexpected++;
	}
	CHECK(np == 3, "three poller processes started");
	CHECK(bad == 0, "stress: %d attach/signal iterations, no ioctl failure", iters);
	CHECK(mismatch == 0, "stress: poll state matched the fence state at every step");
	CHECK(polls_unexpected == 0, "stress: pollers saw no error or abnormal exit");
	fprintf(stderr, "note: stress bad=%d mismatch=%d poller_bad=%lu\n", bad, mismatch, polls_unexpected);
}

int
main(int argc, char **argv)
{
	uint32_t ha = 0, hb = 0;
	int fa = -1, fb = -1, ch, iters = 3000, quick = 0;

	while ((ch = getopt(argc, argv, "s:q")) != -1) {
		if (ch == 's') iters = atoi(optarg);
		else if (ch == 'q') quick = 1;
	}
	fd = open_vgem();
	CHECK(fd >= 0, "found the primary node of the vgem driver");
	if (fd < 0)
		return 1;
	test_basic(&ha, &hb, &fa, &fb);
	if (fa >= 0 && fb >= 0) {
		test_args(ha);
		test_write_fence(ha, hb, fa, fb);
		test_read_fence(ha, fa);
		test_kqueue(ha, fa);
		test_wakeup(ha, fa);
		if (!quick)
			test_expiry(ha, fa);
		test_close_pending();
		test_stress(ha, fa, iters);
	}
	printf("vgem-client: %d checks, %d failed\n", nchecks, nfail);
	return nfail ? 1 : 0;
}
