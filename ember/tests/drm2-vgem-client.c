/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Client for the vgem test driver (sys/modules/vgem): exercises dma_fence,
 * dma_resv and dma-buf through a real DRM driver.  Dumb buffers, PRIME export
 * and import, DRM_IOCTL_VGEM_FENCE_ATTACH (with and without the WRITE flag),
 * DRM_IOCTL_VGEM_FENCE_SIGNAL, poll(2) and kevent(2) on the dma-buf, fence
 * expiry, close with pending fences, and a concurrent stress.  Also the memory
 * mapping of buffer objects (fake offset, mmap, read back, mapping that
 * outlives the handle, fork, bad offsets and sizes) and PRIME between open
 * files (import into the same and another file, mapping of both, closing in
 * every order, mapping of the dma-buf itself) with a threaded stress.
 *
 * The output is one "ok"/"FAIL" line per check, no timings in the line, so
 * the output of different kernels can be compared with diff(1).
 * usage: drm2-vgem-client [-s stress_iterations] [-g gem_stress_iterations] [-q]
 *    (-q: skip the 10 s expiry check)
 * exit status 0 if every check passed.
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/event.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <pthread.h>
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
mkdumb_on(int f, uint32_t w, uint32_t h, uint32_t bpp, uint32_t *handle,
    uint64_t *size)
{
	struct drm_mode_create_dumb cd;
	int r;

	memset(&cd, 0, sizeof cd);
	cd.width = w; cd.height = h; cd.bpp = bpp;
	r = ioctl(f, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0 ? -errno : 0;
	if (r == 0) {
		*handle = cd.handle;
		if (size)
			*size = cd.size;
	}
	return r;
}

static int
mkdumb(uint32_t w, uint32_t h, uint32_t bpp, uint32_t *handle, uint64_t *size)
{

	return mkdumb_on(fd, w, h, bpp, handle, size);
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


/* ------------------------------------------------ mapping and PRIME */

static int
mapdumb(int f, uint32_t handle, uint64_t *off)
{
	struct drm_mode_map_dumb md;

	memset(&md, 0, sizeof md);
	md.handle = handle;
	if (ioctl(f, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0)
		return -errno;
	*off = md.offset;
	return 0;
}

static int
gemclose(int f, uint32_t handle)
{
	struct drm_gem_close gc;

	memset(&gc, 0, sizeof gc);
	gc.handle = handle;
	return ioctl(f, DRM_IOCTL_GEM_CLOSE, &gc) < 0 ? -errno : 0;
}

static int
import_fd(int f, int pfd, uint32_t *handle)
{
	struct drm_prime_handle ph;

	memset(&ph, 0, sizeof ph);
	ph.fd = pfd;
	if (ioctl(f, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph) < 0)
		return -errno;
	*handle = ph.handle;
	return 0;
}

static int
export_on(int f, uint32_t handle, uint32_t flags, int *out)
{
	struct drm_prime_handle ph;

	memset(&ph, 0, sizeof ph);
	ph.handle = handle; ph.flags = flags; ph.fd = -1;
	if (ioctl(f, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph) < 0)
		return -errno;
	*out = ph.fd;
	return 0;
}

/* The mapping of a handle through its fake offset: the address, or NULL with *err = errno. */
static void *
map_handle(int f, uint32_t handle, size_t len, int *err)
{
	uint64_t off;
	void *p;
	int r;

	r = mapdumb(f, handle, &off);
	if (r) {
		*err = -r;
		return NULL;
	}
	p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, f, (off_t)off);
	if (p == MAP_FAILED) {
		*err = errno;
		return NULL;
	}
	return p;
}

static void
fill(volatile uint32_t *p, size_t words, uint32_t seed)
{
	size_t i;

	for (i = 0; i < words; i++)
		p[i] = seed + (uint32_t)i * 0x9e3779b9u;
}

/* Number of words that differ from the pattern of fill(). */
static size_t
verify(volatile uint32_t *p, size_t words, uint32_t seed)
{
	size_t i, bad = 0;

	for (i = 0; i < words; i++)
		if (p[i] != seed + (uint32_t)i * 0x9e3779b9u)
			bad++;
	return bad;
}

static size_t
count_nonzero(volatile uint32_t *p, size_t words)
{
	size_t i, n = 0;

	for (i = 0; i < words; i++)
		n += p[i] != 0;
	return n;
}

/*
 * A mapping that must not give access to an object.  Either mmap fails with
 * EINVAL, or (the 5.6 layer maps an offset that is no object as private
 * anonymous memory) the mapping reads as zeros and a write through it does
 * not reach the object, whose words pv[0..words) hold a nonzero pattern.
 * Returns 1 for either; *how says which.
 */
static int
refused_mapping(void *m, size_t len, volatile uint32_t *pv, size_t words,
    uint32_t seed, const char **how)
{
	volatile uint32_t *q = m;
	size_t i, n = len / 4;
	int ok;

	if (m == MAP_FAILED) {
		*how = errno == EINVAL ? "EINVAL" : "other errno";
		return errno == EINVAL;
	}
	ok = count_nonzero(q, n) == 0;
	for (i = 0; i < n; i++)
		q[i] = 0x5a5a5a5au;
	ok = ok && verify(pv, words, seed) == 0;
	munmap(m, len);
	*how = ok ? "private anonymous memory" : "the mapping aliases an object";
	return ok;
}

#define PG	4096u
#define W64	(16384 / 4)

static void
test_mmap(void)
{
	uint64_t size = 0, off = 0;
	uint32_t h = 0, h2 = 0;
	volatile uint32_t *p, *p2, *pa, *pv;
	void *m;
	int err = 0, r, st;
	pid_t pid;

	CHECK(mkdumb(64, 64, 32, &h, &size) == 0 && size == 16384, "mmap: dumb buffer of 16384 bytes");
	r = mapdumb(fd, h, &off);
	CHECK(r == 0 && off != 0 && (off & (PG - 1)) == 0, "mmap: MAP_DUMB gives a nonzero page aligned fake offset");
	{
		uint64_t off2 = 0;
		CHECK(mapdumb(fd, h, &off2) == 0 && off2 == off, "mmap: MAP_DUMB twice gives the same offset");
	}
	p = map_handle(fd, h, size, &err);
	if (p == NULL)
		fprintf(stderr, "note: mmap of a dumb buffer errno %d\n", err);
	CHECK(p != NULL, "mmap: mapping through the fake offset succeeds");
	if (p == NULL) {
		CHECK(0, "mmap: the other mapping checks need a mapping");
		return;
	}
	CHECK(count_nonzero(p, W64) == 0, "mmap: a new buffer reads as zeros");
	fill(p, W64, 1);
	CHECK(verify(p, W64, 1) == 0, "mmap: written pattern reads back through the same mapping");
	p2 = map_handle(fd, h, size, &err);
	CHECK(p2 != NULL && p2 != p, "mmap: a second mapping of the same object");
	if (p2 == NULL)
		return;
	CHECK(verify(p2, W64, 1) == 0, "mmap: the second mapping sees the data of the first");
	p[3] = 0xfeedf00d;
	CHECK(p2[3] == 0xfeedf00d, "mmap: a write through one mapping is visible through the other");
	p[3] = 1 + 3 * 0x9e3779b9u;
	CHECK(munmap((void *)(uintptr_t)p, size) == 0, "mmap: munmap of the first mapping");
	CHECK(verify(p2, W64, 1) == 0, "mmap: the other mapping survives it");

	/* a child shares the mapping */
	pid = fork();
	if (pid == 0) {
		int bad = (int)verify(p2, W64, 1);
		p2[1] = 0xabad1dea;
		_exit(bad ? 1 : 0);
	}
	waitpid(pid, &st, 0);
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0, "mmap: a forked child sees the mapped data");
	CHECK(p2[1] == 0xabad1dea, "mmap: the write of the child is visible to the parent");
	p2[1] = 1 + 1 * 0x9e3779b9u;

	/* the mapping holds the object when the handle goes */
	r = gemclose(fd, h);
	CHECK(r == 0, "mmap: GEM_CLOSE of the mapped handle");
	CHECK(verify(p2, W64, 1) == 0, "mmap: mapped data remain readable after the handle is closed");
	fill(p2, W64, 7);
	CHECK(verify(p2, W64, 7) == 0, "mmap: the mapping stays writable after the handle is closed");
	m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)off);
	err = m == MAP_FAILED ? errno : 0;
	CHECK(m == MAP_FAILED, "mmap: no new mapping through the offset of a closed handle (errno %d)", err);
	if (m != MAP_FAILED)
		munmap(m, size);
	CHECK(munmap((void *)(uintptr_t)p2, size) == 0, "mmap: munmap of the last mapping frees the object");

	/* the offsets and sizes the object does not have */
	CHECK(mkdumb(64, 64, 32, &h2, NULL) == 0, "mmap: another dumb buffer");
	CHECK(mapdumb(fd, h2, &off) == 0, "mmap: its offset");
	pv = map_handle(fd, h2, size, &err);
	CHECK(pv != NULL, "mmap: map it");
	if (pv == NULL)
		return;
	fill(pv, size / 4, 31);
	{
		const char *how = "";
		int ok;

		m = mmap(NULL, size + PG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)off);
		ok = refused_mapping(m, size + PG, pv, size / 4, 31, &how);
		CHECK(ok, "mmap: a mapping longer than the object is refused (%s)", how);
		fprintf(stderr, "note: longer than the object: %s\n", how);
		m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)off + PG);
		ok = refused_mapping(m, size, pv, size / 4, 31, &how);
		CHECK(ok, "mmap: a mapping that starts inside the object is refused (%s)", how);
		fprintf(stderr, "note: starts inside the object: %s\n", how);
		m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		ok = refused_mapping(m, size, pv, size / 4, 31, &how);
		CHECK(ok, "mmap: offset 0 is not an object, the mapping is refused (%s)", how);
		fprintf(stderr, "note: offset 0: %s\n", how);
		m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)off + (1ll << 36));
		ok = refused_mapping(m, size, pv, size / 4, 31, &how);
		CHECK(ok, "mmap: an offset beyond all objects is refused (%s)", how);
		fprintf(stderr, "note: beyond all objects: %s\n", how);
	}
	/* the first page alone is fine, read only too */
	pa = mmap(NULL, PG, PROT_READ, MAP_SHARED, fd, (off_t)off);
	CHECK(pa != (void *)MAP_FAILED && count_nonzero(pa, PG / 4) == 0, "mmap: the first page alone, read only");
	if (pa != (void *)MAP_FAILED)
		munmap((void *)(uintptr_t)pa, PG);
	CHECK(verify(pv, size / 4, 31) == 0, "mmap: no refused mapping changed the object");
	munmap((void *)(uintptr_t)pv, size);
	CHECK(gemclose(fd, h2) == 0, "mmap: GEM_CLOSE of the second buffer");
}

/* PRIME between open files: the contents seen through the mappings of both. */
static void
test_prime(void)
{
	uint64_t size = 0;
	uint32_t ha = 0, hb = 0, hc = 0, hd = 0, he = 0;
	int f2, f3, pfd = -1, pfd2 = -1, err = 0, r, dummy;
	volatile uint32_t *p1, *p2, *p3;
	struct drm_prime_handle ph;

	f2 = open_vgem();
	CHECK(f2 >= 0, "prime: second open of the primary node");
	if (f2 < 0)
		return;
	CHECK(mkdumb(64, 64, 32, &ha, &size) == 0, "prime: dumb buffer on the first open");
	p1 = map_handle(fd, ha, size, &err);
	CHECK(p1 != NULL, "prime: map the first buffer");
	if (p1 == NULL) {
		close(f2);
		return;
	}
	fill(p1, W64, 11);
	CHECK(export_on(fd, ha, O_CLOEXEC | O_RDWR, &pfd) == 0 && pfd >= 0, "prime: export it");
	CHECK(export_on(fd, ha, O_CLOEXEC, &pfd2) == 0 && pfd2 >= 0 && pfd2 != pfd, "prime: a second export gives another descriptor");
	r = import_fd(f2, pfd, &hb);
	CHECK(r == 0 && hb != 0, "prime: import into another open file");
	r = import_fd(f2, pfd2, &hc);
	CHECK(r == 0 && hc == hb, "prime: both descriptors import as the same handle");
	r = import_fd(f2, pfd, &hd);
	CHECK(r == 0 && hd == hb, "prime: a repeated import gives the same handle");
	p2 = map_handle(f2, hb, size, &err);
	if (p2 == NULL)
		fprintf(stderr, "note: mmap of an imported handle errno %d\n", err);
	CHECK(p2 != NULL, "prime: map the imported handle");
	if (p2 != NULL) {
		CHECK(verify(p2, W64, 11) == 0, "prime: the importer sees the data of the exporter");
		p2[5] = 0x12345678;
		CHECK(p1[5] == 0x12345678, "prime: a write by the importer is visible to the exporter");
		p1[5] = 11 + 5 * 0x9e3779b9u;
	}
	/* mapping of the dma-buf itself */
	{
		volatile uint32_t *pd = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, pfd, 0);

		err = pd == (void *)MAP_FAILED ? errno : 0;
		CHECK(pd != (void *)MAP_FAILED, "prime: mmap of the dma-buf descriptor (errno %d)", err);
		if (pd != (void *)MAP_FAILED) {
			CHECK(verify(pd, W64, 11) == 0, "prime: the dma-buf mapping sees the same data");
			pd[7] = 0x600dcafe;
			CHECK(p1[7] == 0x600dcafe, "prime: a write through the dma-buf mapping reaches the object");
			p1[7] = 11 + 7 * 0x9e3779b9u;
			munmap((void *)(uintptr_t)pd, size);
		}
	}
	/* the object is shared: closing in a different order */
	CHECK(gemclose(fd, ha) == 0, "prime: close the handle of the exporter");
	r = import_fd(f2, pfd, &hd);
	CHECK(r == 0 && hd == hb, "prime: the importer's handle is unchanged after the exporter closed");
	if (p2 != NULL)
		CHECK(verify(p2, W64, 11) == 0, "prime: data intact after the exporter closed its handle");
	CHECK(gemclose(f2, hb) == 0, "prime: close the importer's handle");
	/* both handles are gone: the descriptors hold the object */
	r = import_fd(fd, pfd2, &he);
	CHECK(r == 0 && he != 0, "prime: import into the first file after all handles were closed");
	p3 = map_handle(fd, he, size, &err);
	CHECK(p3 != NULL, "prime: map the re-imported object");
	if (p3 != NULL) {
		CHECK(verify(p3, W64, 11) == 0, "prime: the data outlived every handle");
		munmap((void *)(uintptr_t)p3, size);
	}
	munmap((void *)(uintptr_t)p1, size);
	if (p2 != NULL)
		munmap((void *)(uintptr_t)p2, size);
	CHECK(gemclose(fd, he) == 0, "prime: close the re-imported handle");
	close(pfd2);
	/* the exporting file goes away; the importer keeps working */
	f3 = open_vgem();
	CHECK(f3 >= 0, "prime: third open of the primary node");
	if (f3 >= 0) {
		uint32_t h3 = 0, hi = 0;
		volatile uint32_t *q1, *q2;

		CHECK(mkdumb_on(f3, 32, 32, 32, &h3, &size) == 0, "prime: buffer on the third open");
		q1 = map_handle(f3, h3, size, &err);
		CHECK(q1 != NULL, "prime: map the buffer of the third open");
		if (q1 != NULL) {
			fill(q1, size / 4, 21);
			CHECK(export_on(f3, h3, O_CLOEXEC, &pfd2) == 0, "prime: export it");
			munmap((void *)(uintptr_t)q1, size);
			close(f3);
			r = import_fd(fd, pfd2, &hi);
			CHECK(r == 0, "prime: import after the exporting file was closed");
			q2 = r == 0 ? map_handle(fd, hi, size, &err) : NULL;
			CHECK(q2 != NULL && verify(q2, size / 4, 21) == 0,
			    "prime: the data outlived the file that made the object");
			if (q2 != NULL)
				munmap((void *)(uintptr_t)q2, size);
			if (r == 0)
				gemclose(fd, hi);
			close(pfd2);
		} else {
			close(f3);
		}
	}
	/* invalid arguments */
	dummy = open("/dev/null", O_RDWR);
	r = import_fd(fd, dummy, &hd);
	CHECK(r == -EINVAL, "prime: import of a descriptor that is not a dma-buf is EINVAL (got %d)", r);
	if (dummy >= 0)
		close(dummy);
	r = import_fd(fd, 4000, &hd);
	CHECK(r == -EBADF, "prime: import of a closed descriptor is EBADF (got %d)", r);
	memset(&ph, 0, sizeof ph);
	ph.handle = hb; ph.flags = 0x40000000; ph.fd = -1;
	r = ioc(DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph);
	CHECK(r == -EINVAL, "prime: export with an unknown flag is EINVAL (got %d)", r);
	r = export_on(f2, hb, O_CLOEXEC, &dummy);
	CHECK(r == -ENOENT, "prime: export of a closed handle is ENOENT (got %d)", r);
	close(pfd);
	close(f2);
}

/* ------------------------------------------------ threaded mapping stress */

struct gst {
	int	iters;
	int	id;
	int	bad;
};

static void *
gem_worker(void *arg)
{
	struct gst *g = arg;
	int f2 = open_vgem();
	int i, err;

	if (f2 < 0) {
		g->bad++;
		return NULL;
	}
	for (i = 0; i < g->iters; i++) {
		uint32_t h = 0, hi = 0, hj = 0;
		uint64_t size = 0;
		int pfd = -1;
		uint32_t seed = (uint32_t)(g->id * 7919 + i);
		volatile uint32_t *p1 = NULL, *p2 = NULL;
		int ok = 1;
		uint32_t dim = 8 + (uint32_t)(i % 4) * 8;	/* 256..4096 bytes */

		if (mkdumb(dim, dim, 32, &h, &size) != 0) { g->bad++; continue; }
		p1 = map_handle(fd, h, size, &err);
		if (p1 == NULL) { g->bad++; gemclose(fd, h); continue; }
		fill(p1, size / 4, seed);
		if (export_on(fd, h, O_CLOEXEC, &pfd) != 0) { g->bad++; ok = 0; goto out1; }
		if (import_fd(f2, pfd, &hi) != 0) { g->bad++; ok = 0; goto out2; }
		if (import_fd(fd, pfd, &hj) != 0 || hj != h) g->bad++;
		p2 = map_handle(f2, hi, size, &err);
		if (p2 == NULL) { g->bad++; ok = 0; goto out3; }
		if (verify(p2, size / 4, seed) != 0) g->bad++;
		p2[0] ^= 0xffffffffu;
		if (p1[0] != p2[0]) g->bad++;
		/* close in an order that depends on the iteration */
		switch (i % 3) {
		case 0:
			munmap((void *)(uintptr_t)p1, size);
			gemclose(fd, h);
			close(pfd); pfd = -1;
			gemclose(f2, hi);
			munmap((void *)(uintptr_t)p2, size);
			break;
		case 1:
			gemclose(f2, hi);
			munmap((void *)(uintptr_t)p2, size);
			close(pfd); pfd = -1;
			gemclose(fd, h);
			munmap((void *)(uintptr_t)p1, size);
			break;
		default:
			close(pfd); pfd = -1;
			gemclose(fd, h);
			gemclose(f2, hi);
			munmap((void *)(uintptr_t)p2, size);
			munmap((void *)(uintptr_t)p1, size);
			break;
		}
		continue;
out3:		gemclose(f2, hi);
out2:		close(pfd);
out1:		(void)ok;
		munmap((void *)(uintptr_t)p1, size);
		gemclose(fd, h);
	}
	close(f2);
	return NULL;
}

static void
test_gem_stress(int iters)
{
	enum { NT = 4 };
	pthread_t th[NT];
	struct gst g[NT];
	int i, total_bad = 0, started = 0;

	if (iters <= 0)
		return;
	for (i = 0; i < NT; i++) {
		g[i].iters = iters / NT; g[i].id = i; g[i].bad = 0;
		if (pthread_create(&th[i], NULL, gem_worker, &g[i]) == 0)
			started++;
		else
			g[i].iters = 0;
	}
	for (i = 0; i < NT; i++)
		if (g[i].iters)
			pthread_join(th[i], NULL);
	for (i = 0; i < NT; i++)
		total_bad += g[i].bad;
	CHECK(started == NT, "gem stress: %d threads started", NT);
	CHECK(total_bad == 0, "gem stress: %d create/map/export/import/close cycles, no failure", iters / NT * NT);
	fprintf(stderr, "note: gem stress bad=%d\n", total_bad);
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
	int fa = -1, fb = -1, ch, iters = 3000, quick = 0, giters = 2000;

	while ((ch = getopt(argc, argv, "s:g:q")) != -1) {
		if (ch == 's') iters = atoi(optarg);
		else if (ch == 'g') giters = atoi(optarg);
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
	test_mmap();
	test_prime();
	test_gem_stress(giters);
	printf("vgem-client: %d checks, %d failed\n", nchecks, nfail);
	return nfail ? 1 : 0;
}
