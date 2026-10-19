/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted. Real native ioctl and reservation bodies. */
#define main array_contract_main
#include "drm-fence-array-contract.c"
#undef main
#include "dmabuf-abi.h"
#include <limits.h>
#include <sys/types.h>

typedef unsigned long u_long;
typedef int kmutex_t;
struct selinfo {
	int live;
};
struct file {
	int f_type, f_flag;
	const void *f_ops;
	void *f_data;
};
struct ww_mutex {
	spinlock_t lock;
};
struct seqcount {
	unsigned value;
};
struct rcu_head {
	int unused;
};
#define __rcu
#include "sync-layout.h"
struct dma_buf {
	struct dma_resv *resv;
};
#define KM_SLEEP 0
#define MUTEX_DEFAULT 0
#define IPL_VM 0
#define DTYPE_MISC 5
#define FREAD 1
#define FWRITE 2
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define atomic_store_relaxed(p, v) (*(p) = (v))
#define write_seqcount_begin(s)                                                \
	do {                                                                   \
		CHECK(((s)->value & 1) == 0);                                  \
		++(s)->value;                                                  \
	} while (0)
#define write_seqcount_end(s)                                                  \
	do {                                                                   \
		CHECK(((s)->value & 1) == 1);                                  \
		++(s)->value;                                                  \
	} while (0)
static unsigned vector_calls, fail_vector, fd_calls, file_count, aborts;
static unsigned sync_allocs, sync_frees, lock_calls, get_calls, put_calls;
static bool fail_fd, fail_sync;
static int lock_error, reserve_error, curlwp, curproc, sync_file_ops;
static struct file fd_table[32];
static bool fd_used[32], fd_visible[32], fd_cloexec[32];
static unsigned fd_refs[32];
static struct child *stub;
static void (*lock_hook)(struct dma_resv *);
static void *
kmalloc_array(size_t n, size_t size, int flags)
{
	(void)flags;
	if (++vector_calls == fail_vector || n > SIZE_MAX / size)
		return NULL;
	return calloc(n, size);
}
static void *
kmem_alloc(size_t n, int flags)
{
	(void)flags;
	void *p = malloc(n);
	CHECK(p);
	return p;
}
static void *
kmem_zalloc(size_t n, int flags)
{
	(void)flags;
	void *p = calloc(1, n);
	CHECK(p);
	sync_allocs++;
	return p;
}
static void
kmem_free(void *p, size_t n)
{
	(void)n;
	free(p);
}
static void
mutex_init(int *p, int a, int b)
{
	(void)a;
	(void)b;
	*p = 1;
}
static void
mutex_destroy(int *p)
{
	CHECK(*p == 1);
	*p = 0;
}
static void
selinit(struct selinfo *s)
{
	s->live = 1;
}
static void
seldestroy(struct selinfo *s)
{
	CHECK(s->live);
	s->live = 0;
}
static void
dma_fence_remove_callback(struct dma_fence *f, struct dma_fence_cb *cb)
{
	(void)f;
	(void)cb;
	CHECK(false);
}
static struct dma_fence *
dma_fence_get_stub(void)
{
	return dma_fence_get(&stub->base);
}
static int
dma_fence_get_status(struct dma_fence *f)
{
	return fixture_status(f);
}
static bool
dma_fence_is_later(const struct dma_fence *a, const struct dma_fence *b)
{
	CHECK(a->context == b->context);
	return (int64_t)(a->seqno - b->seqno) > 0;
}
static uint64_t
dma_fence_context_alloc(unsigned n)
{
	static uint64_t next = UINT64_C(0x1234567800000000);
	uint64_t v = next;
	next += n;
	return v;
}
static bool
dma_resv_held(struct dma_resv *r)
{
	return spin_is_locked(&r->lock.lock);
}
static void
dma_resv_assert_held(struct dma_resv *r)
{
	CHECK(dma_resv_held(r));
}
static int
dma_resv_lock_interruptible(struct dma_resv *r, void *ctx)
{
	(void)ctx;
	lock_calls++;
	if (lock_error)
		return -lock_error;
	spin_lock(&r->lock.lock);
	if (lock_hook)
		lock_hook(r);
	return 0;
}
static void
dma_resv_unlock(struct dma_resv *r)
{
	spin_unlock(&r->lock.lock);
}
static void
objlist_defer_free(struct dma_resv_list *p)
{
	free(p);
}
#include "resv-bodies.h"
static int
dma_resv_reserve_shared(struct dma_resv *r, unsigned n)
{
	return reserve_error ? -reserve_error : production_reserve_shared(r, n);
}
static int
fd_allocfile(struct file **fp, int *fd)
{
	fd_calls++;
	if (fail_fd)
		return EMFILE;
	for (unsigned i = 0; i < 32; i++)
		if (!fd_used[i]) {
			fd_used[i] = true;
			fd_visible[i] = false;
			fd_cloexec[i] = false;
			fd_refs[i] = 0;
			memset(&fd_table[i], 0, sizeof(fd_table[i]));
			*fp = &fd_table[i];
			*fd = (int)i;
			file_count++;
			return 0;
		}
	return EMFILE;
}
static void
fd_affix(int p, const struct file *fp, int fd)
{
	(void)p;
	CHECK(fd_used[fd] && !fd_visible[fd] && fp == &fd_table[fd]);
	fd_visible[fd] = true;
}
static void
fd_abort(int p, const struct file *fp, int fd)
{
	(void)p;
	CHECK(fd_used[fd] && !fd_visible[fd] && fp == &fd_table[fd]);
	CHECK(!fp->f_data);
	fd_used[fd] = false;
	file_count--;
	aborts++;
}
static void
fd_set_exclose(int p, int fd, bool value)
{
	(void)p;
	CHECK(fd_used[fd]);
	fd_cloexec[fd] = value;
}
static struct file *
fd_getfile(int fd)
{
	get_calls++;
	if (fd < 0 || fd >= 32 || !fd_visible[fd])
		return NULL;
	fd_refs[fd]++;
	return &fd_table[fd];
}
static void
fd_putfile(int fd)
{
	CHECK(fd_refs[fd]);
	fd_refs[fd]--;
	put_calls++;
}
#include "sync-file-bodies.h"
static struct sync_file *
sync_file_create(struct dma_fence *f, struct file *fp)
{
	return fail_sync ? NULL : production_sync_file_create(f, fp);
}
static int
fbadop_ioctl(struct file *fp, u_long cmd, void *data)
{
	(void)fp;
	(void)cmd;
	(void)data;
	return ENOTTY;
}
#include "ioctl-bodies.h"
#include "ioctl-dispatch.h"
static void
close_fd(int fd)
{
	CHECK(fd_visible[fd] && !fd_refs[fd]);
	CHECK(sync_file_close(&fd_table[fd]) == 0);
	sync_frees++;
	fd_visible[fd] = false;
	fd_used[fd] = false;
	file_count--;
}
static int
make_sync(struct dma_fence *f)
{
	struct file *fp;
	int fd;
	CHECK(fd_allocfile(&fp, &fd) == 0);
	CHECK(sync_file_create(f, fp));
	fd_affix(0, fp, fd);
	return fd;
}
static void
resv_init(struct dma_resv *r)
{
	memset(r, 0, sizeof(*r));
	spin_lock_init(&r->lock.lock);
}
static void
resv_fini(struct dma_resv *r)
{
	CHECK(dma_resv_lock_interruptible(r, NULL) == 0);
	dma_resv_add_excl_fence(r, NULL);
	dma_resv_unlock(r);
	free(r->fence);
	free(r->robj_prealloc);
	spin_lock_destroy(&r->lock.lock);
}
static void
set_fences(struct dma_resv *r, struct dma_fence *w, struct dma_fence *read)
{
	CHECK(dma_resv_lock_interruptible(r, NULL) == 0);
	dma_resv_add_excl_fence(r, w);
	if (read) {
		CHECK(dma_resv_reserve_shared(r, 1) == 0);
		dma_resv_add_shared_fence(r, read);
	}
	dma_resv_unlock(r);
}
static int
exported(struct file *fp, unsigned flags)
{
	struct dma_buf_export_sync_file args = {flags, -77};
	CHECK(actual_ioctl(fp, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &args) == 0);
	CHECK(args.fd >= 0 && fd_visible[args.fd] && fd_cloexec[args.fd]);
	return args.fd;
}
static struct dma_fence *
fd_fence(int fd)
{
	return ((struct sync_file *)fd_table[fd].f_data)->sf_fence;
}
static void
imported(struct file *fp, unsigned flags, int fd)
{
	struct dma_buf_import_sync_file a = {flags, fd};
	CHECK(actual_ioctl(fp, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &a) == 0);
	CHECK((((struct dma_buf *)fp->f_data)->resv->seq.value & 1) == 0);
}
static void
validate_access(struct file *fp)
{
	for (unsigned rights = 0; rights <= 3; rights++)
		for (unsigned flags = 0; flags < 8; flags++) {
			fp->f_flag = (int)rights;
			unsigned before = fd_calls;
			struct dma_buf_export_sync_file ex = {flags, -77};
			struct dma_buf_import_sync_file in = {flags, -999};
			int expected = flags == 0 || flags > 3
					   ? EINVAL
					   : ((flags & ~rights) ? EACCES : 0);
			int e = actual_ioctl(fp, DMA_BUF_IOCTL_EXPORT_SYNC_FILE,
					     &ex);
			CHECK(e == expected);
			if (expected) {
				CHECK(ex.fd == -77 && fd_calls == before);
			} else
				close_fd(ex.fd);
			CHECK(actual_ioctl(fp, DMA_BUF_IOCTL_IMPORT_SYNC_FILE,
					   &in) ==
			      (expected ? expected : EINVAL));
		}
	fp->f_flag = FREAD | FWRITE;
	CHECK(actual_ioctl(fp, 0, NULL) == fbadop_ioctl(fp, 0, NULL));
}
int
main(void)
{
	struct dma_resv r;
	resv_init(&r);
	struct dma_buf b = {&r};
	struct file fp = {DTYPE_MISC, FREAD | FWRITE, NULL, &b};
	stub = child_new(0);
	child_signal(stub);
	validate_access(&fp);
	CHECK(sizeof(struct dma_buf_export_sync_file) == 8 &&
	      sizeof(struct dma_buf_import_sync_file) == 8);
	CHECK(DMA_BUF_IOCTL_EXPORT_SYNC_FILE == 0xc0086202UL &&
	      DMA_BUF_IOCTL_IMPORT_SYNC_FILE == 0x80086203UL);
	int fd = exported(&fp, DMA_BUF_SYNC_READ);
	CHECK(fd_fence(fd) == &stub->base);
	close_fd(fd);
	struct child *w = child_new(-EIO), *rd = child_new(0),
		     *incoming = child_new(0);
	w->base.context = 10;
	rd->base.context = 20;
	incoming->base.context = 30;
	set_fences(&r, &w->base, &rd->base);
	fd = exported(&fp, DMA_BUF_SYNC_READ);
	CHECK(fd_fence(fd) == &w->base);
	close_fd(fd);
	int input = make_sync(&incoming->base);
	imported(&fp, DMA_BUF_SYNC_WRITE, input);
	CHECK(r.fence && r.fence->shared_count == 1 &&
	      r.fence->shared[0] == &rd->base);
	CHECK(dma_fence_is_array(r.fence_excl));
	for (unsigned cycle = 0; cycle < 128; cycle++) {
		fd = exported(&fp, DMA_BUF_SYNC_WRITE);
		imported(&fp, DMA_BUF_SYNC_WRITE, fd);
		close_fd(fd);
		struct dma_fence_array *a = to_dma_fence_array(r.fence_excl);
		CHECK(a->num_fences == 3);
		for (unsigned i = 0; i < a->num_fences; i++)
			CHECK(!dma_fence_is_array(a->fences[i]));
	}
	fd = exported(&fp, DMA_BUF_SYNC_WRITE);
	struct dma_fence *f = fd_fence(fd);
	enable(f);
	child_signal(w);
	CHECK(fixture_status(f) == 0);
	child_signal(rd);
	CHECK(fixture_status(f) == 0);
	child_signal(incoming);
	drain();
	CHECK(fixture_status(f) == -EIO);
	close_fd(fd);
	close_fd(input);
	set_fences(&r, NULL, NULL);
	dma_fence_put(&w->base);
	dma_fence_put(&rd->base);
	dma_fence_put(&incoming->base);
	/* READ imports preserve a newer same-context shared fence. */
	struct child *newer = child_new(0), *older = child_new(0);
	newer->base.seqno = 9;
	older->base.seqno = 3;
	set_fences(&r, NULL, &newer->base);
	input = make_sync(&older->base);
	imported(&fp, DMA_BUF_SYNC_READ, input);
	CHECK(r.fence->shared_count == 1 && r.fence->shared[0] == &newer->base);
	close_fd(input);
	older->base.seqno = 10;
	input = make_sync(&older->base);
	imported(&fp, DMA_BUF_SYNC_READ, input);
	CHECK(r.fence->shared_count == 1 && r.fence->shared[0] == &older->base);
	close_fd(input);
	set_fences(&r, NULL, NULL);
	dma_fence_put(&newer->base);
	dma_fence_put(&older->base);
	/* An OR child remains one dependency of an AND export. */
	struct child *a = child_new(0), *c = child_new(0),
		     *writer = child_new(0);
	struct dma_fence **children = calloc(2, sizeof(*children));
	CHECK(children);
	children[0] = dma_fence_get(&a->base);
	children[1] = dma_fence_get(&c->base);
	struct dma_fence_array *any =
	    dma_fence_array_create(2, children, 100, 1, true);
	CHECK(any);
	set_fences(&r, &writer->base, NULL);
	input = make_sync(&any->base);
	imported(&fp, DMA_BUF_SYNC_WRITE, input);
	close_fd(input);
	fd = exported(&fp, DMA_BUF_SYNC_READ);
	f = fd_fence(fd);
	CHECK(to_dma_fence_array(f)->num_fences == 2);
	enable(f);
	child_signal(writer);
	CHECK(fixture_status(f) == 0);
	child_signal(a);
	drain();
	CHECK(fixture_status(f) == 1);
	child_signal(c);
	drain();
	close_fd(fd);
	set_fences(&r, NULL, NULL);
	dma_fence_put(&any->base);
	dma_fence_put(&a->base);
	dma_fence_put(&c->base);
	dma_fence_put(&writer->base);
	/* Failure paths keep the output descriptor and reservation unchanged.
	 */
	struct child *pending = child_new(0), *extra = child_new(0);
	set_fences(&r, &pending->base, NULL);
	input = make_sync(&extra->base);
	for (unsigned mode = 0; mode < 6; mode++) {
		struct dma_buf_export_sync_file ex = {DMA_BUF_SYNC_WRITE, -77};
		const struct dma_fence *before = r.fence_excl;
		fail_fd = mode == 0;
		lock_error = mode == 1 ? EINTR : 0;
		fail_vector = mode == 2 ? vector_calls + 1 : 0;
		fail_sync = mode == 3;
		if (mode < 4) {
			CHECK(actual_ioctl(&fp, DMA_BUF_IOCTL_EXPORT_SYNC_FILE,
					   &ex) != 0);
			CHECK(ex.fd == -77);
		} else {
			struct dma_buf_import_sync_file in = {
			    DMA_BUF_SYNC_WRITE, input};
			fail_vector = mode == 4 ? vector_calls + 1 : 0;
			allocation_failure = mode == 5;
			CHECK(actual_ioctl(&fp, DMA_BUF_IOCTL_IMPORT_SYNC_FILE,
					   &in) == ENOMEM);
		}
		CHECK(r.fence_excl == before && pending->base.refs == 2 &&
		      extra->base.refs == 2);
		fail_fd = false;
		lock_error = 0;
		fail_vector = 0;
		fail_sync = false;
		allocation_failure = false;
	}
	reserve_error = ENOMEM;
	struct dma_buf_import_sync_file in = {DMA_BUF_SYNC_READ, input};
	CHECK(actual_ioctl(&fp, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &in) == ENOMEM);
	reserve_error = 0;
	CHECK(r.fence_excl == &pending->base);
	close_fd(input);
	set_fences(&r, NULL, NULL);
	dma_fence_put(&pending->base);
	dma_fence_put(&extra->base);
	/* Every vector growth failure keeps all original reservation refs. */
	struct child *many[20];
	CHECK(dma_resv_lock_interruptible(&r, NULL) == 0);
	for (unsigned i = 0; i < 20; i++) {
		many[i] = child_new(0);
		many[i]->base.context = 1000 + i;
		CHECK(dma_resv_reserve_shared(&r, 1) == 0);
		dma_resv_add_shared_fence(&r, &many[i]->base);
	}
	dma_resv_unlock(&r);
	for (unsigned fail = 1; fail <= 6; fail++) {
		struct dma_buf_export_sync_file ex = {DMA_BUF_SYNC_WRITE, -77};
		fail_vector = vector_calls + fail;
		CHECK(actual_ioctl(&fp, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex) ==
		      ENOMEM);
		CHECK(ex.fd == -77);
		CHECK(r.fence->shared_count == 20);
		for (unsigned i = 0; i < 20; i++)
			CHECK(many[i]->base.refs == 2);
		fail_vector = 0;
	}
	set_fences(&r, NULL, NULL);
	for (unsigned i = 0; i < 20; i++)
		dma_fence_put(&many[i]->base);
	/* A foreign file must not expose a field that resembles a fence
	 * pointer. */
	for (unsigned variant = 0; variant < 3; variant++) {
		struct file *foreign;
		int foreign_fd;
		CHECK(fd_allocfile(&foreign, &foreign_fd) == 0);
		foreign->f_type = variant == 0 ? 1 : DTYPE_MISC;
		foreign->f_ops = variant == 1 ? (const void *)&fp
					      : (const void *)&sync_file_ops;
		foreign->f_data = NULL;
		fd_affix(0, foreign, foreign_fd);
		struct dma_buf_import_sync_file bad = {DMA_BUF_SYNC_WRITE,
						       foreign_fd};
		unsigned puts = put_calls;
		CHECK(actual_ioctl(&fp, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &bad) ==
		      EINVAL);
		CHECK(put_calls == puts + 1 && fd_refs[foreign_fd] == 0 &&
		      r.fence_excl == NULL);
		fd_visible[foreign_fd] = false;
		fd_used[foreign_fd] = false;
		file_count--;
	}
	/* A snapshot keeps the old dependency after a new reservation update.
	 */
	struct child *first = child_new(-EIO), *second = child_new(0);
	set_fences(&r, &first->base, NULL);
	fd = exported(&fp, DMA_BUF_SYNC_READ);
	set_fences(&r, &second->base, NULL);
	CHECK(fd_fence(fd) == &first->base);
	child_signal(first);
	CHECK(fixture_status(fd_fence(fd)) == -EIO);
	close_fd(fd);
	set_fences(&r, &first->base, NULL);
	fd = exported(&fp, DMA_BUF_SYNC_READ);
	CHECK(fixture_status(fd_fence(fd)) == -EIO);
	close_fd(fd);
	set_fences(&r, NULL, NULL);
	dma_fence_put(&first->base);
	dma_fence_put(&second->base);
	resv_fini(&r);
	dma_fence_put(&stub->base);
	CHECK(!file_count && sync_allocs == sync_frees &&
	      allocations == releases);
	printf("native DMA-BUF sync: %u checks PASS\n", checks);
	return 0;
}
