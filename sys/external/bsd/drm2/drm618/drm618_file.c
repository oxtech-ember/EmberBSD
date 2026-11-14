/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface in
 * include/drm618_file.h, no Linux code.
 *
 * The Linux file model of the 6.18 core on NetBSD files and descriptors:
 * Linux files (struct linux_file here, struct file in the 6.18 sources),
 * their descriptors, the cdevsw that opens them and the file operations of
 * NetBSD that call the Linux ones.  Built with -DDRM618_NBSD_FILE (see
 * drm618_file.h): this is the one source that sees both kinds of file.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/ioccom.h>
#include <sys/kauth.h>
#include <sys/kmem.h>
#include <sys/lwp.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/select.h>
#include <sys/specificdata.h>
#include <sys/systm.h>

#include <drm618_file.h>

/* ---- Linux files ------------------------------------------------------ */

static struct linux_file *
file_alloc(const struct file_operations *fops, unsigned int flags,
    void *private_data, struct inode *inode)
{
	struct linux_file *lf;

	lf = kmem_zalloc(sizeof(*lf), KM_SLEEP);
	lf->f_refcnt = 1;
	lf->f_op = fops;
	lf->f_flags = flags;
	lf->f_mode = ((flags & O_ACCMODE) == O_RDONLY ? FMODE_READ :
	    (flags & O_ACCMODE) == O_WRONLY ? FMODE_WRITE :
	    FMODE_READ | FMODE_WRITE);
	lf->private_data = private_data;
	if (inode != NULL) {
		lf->f_inode = inode;
		lf->f_mapping = inode->i_mapping;
		lf->f_inode_owned = true;
	}
	return lf;
}

static struct inode *
inode_alloc(dev_t rdev, mode_t mode)
{
	struct inode *inode;

	inode = kmem_zalloc(sizeof(*inode), KM_SLEEP);
	inode->i_rdev = rdev;
	inode->i_mode = mode;
	inode->i_mapping = &inode->i_data;
	inode->i_refcnt = 1;
	return inode;
}

struct linux_file *
drm618_file_alloc(const struct file_operations *fops, unsigned int flags,
    void *private_data)
{

	return file_alloc(fops, flags, private_data, NULL);
}

struct linux_file *
anon_inode_getfile(const char *name __unused, const struct file_operations *fops,
    void *priv, int flags)
{

	return file_alloc(fops, flags, priv, inode_alloc(NODEV, 0));
}

struct linux_file *
get_file(struct linux_file *lf)
{

	atomic_inc_uint(&lf->f_refcnt);
	return lf;
}

void
fput(struct linux_file *lf)
{

	membar_release();
	if (atomic_dec_uint_nv(&lf->f_refcnt) != 0)
		return;
	membar_acquire();
	if (lf->f_op != NULL && lf->f_op->release != NULL)
		(void)(*lf->f_op->release)(lf->f_inode, lf);
	if (lf->f_inode_owned)
		kmem_free(lf->f_inode, sizeof(*lf->f_inode));
	kmem_free(lf, sizeof(*lf));
}

loff_t
noop_llseek(struct linux_file *lf __unused, loff_t offset __unused,
    int whence __unused)
{

	return 0;
}

/*
 * A Linux file for a device node: the inode carries the device, f_op->open
 * runs on it.  Returns 0 or a negative errno; the file is not released
 * (f_op->release does not run) when open fails.
 */
int
drm618_file_open(const struct file_operations *fops, dev_t dev,
    unsigned int flags, struct linux_file **lfp)
{
	struct linux_file *lf;
	int error = 0;

	lf = file_alloc(fops, flags, NULL, inode_alloc(dev, S_IFCHR));
	if (lf->f_op->open != NULL)
		error = (*lf->f_op->open)(lf->f_inode, lf);
	if (error) {
		kmem_free(lf->f_inode, sizeof(*lf->f_inode));
		kmem_free(lf, sizeof(*lf));
		return error;
	}
	*lfp = lf;
	return 0;
}

/* ---- NetBSD files of Linux files ---------------------------------- */

static const struct fileops drm618_fileops;

#define	LF(fp)	((struct linux_file *)(fp)->f_data)

static int
drm618_fo_read(struct file *fp, off_t *offp, struct uio *uio, kauth_cred_t cred,
    int flags)
{
	struct linux_file *lf = LF(fp);
	loff_t pos = *offp;
	ssize_t n;

	if (lf->f_op->read == NULL)
		return EINVAL;
	/* One user buffer of this process: what Linux read operations take. */
	if (uio->uio_iovcnt != 1 || VMSPACE_IS_KERNEL_P(uio->uio_vmspace) ||
	    uio->uio_vmspace != curproc->p_vmspace)
		return ENOTSUP;
	lf->f_flags = (lf->f_flags & ~O_NONBLOCK) |
	    ((fp->f_flag & FNONBLOCK) ? O_NONBLOCK : 0);
	n = (*lf->f_op->read)(lf, uio->uio_iov->iov_base, uio->uio_iov->iov_len,
	    &pos);
	if (n < 0)
		return (int)-n;
	uio->uio_iov->iov_base = (char *)uio->uio_iov->iov_base + n;
	uio->uio_iov->iov_len -= n;
	uio->uio_resid -= n;
	return 0;
}

/*
 * ioctl data is copied in and out by NetBSD, but Linux ioctls copy it
 * themselves (drm_ioctl reads and writes the user pointer): the data is
 * given as the "user" pointer, and copy_from_user/copy_to_user (see
 * drm618_prefix.h) treat exactly that pointer, within the length NetBSD
 * copied, as kernel memory.  The window is per lwp; a pointer nested in the
 * data is a user address and goes through copyin/copyout as always.
 */
struct kwindow {
	const void	*kw_ptr;
	size_t		kw_len;
};

static specificdata_key_t kw_key;
static ONCE_DECL(kw_once);

static int
kw_init(void)
{

	return lwp_specific_key_create(&kw_key, NULL);
}

static struct kwindow *
kw_get(void)
{

	if (RUN_ONCE(&kw_once, kw_init) != 0)
		return NULL;
	return lwp_getspecific(kw_key);
}

int
drm618_copy_from_user(void *to, const void *from, size_t n)
{
	struct kwindow *kw = kw_get();

	if (kw != NULL && from == kw->kw_ptr && n <= kw->kw_len) {
		memcpy(to, from, n);
		return 0;
	}
	return -copyin(from, to, n);
}

int
drm618_copy_to_user(void *to, const void *from, size_t n)
{
	struct kwindow *kw = kw_get();

	if (kw != NULL && to == kw->kw_ptr && n <= kw->kw_len) {
		memcpy(to, from, n);
		return 0;
	}
	return -copyout(from, to, n);
}

/*
 * Run an ioctl of Linux file operations; data and cmd as NetBSD gives
 * them.  Exported for the test of the window.
 */
int
drm618_file_ioctl(struct linux_file *lf, u_long cmd, void *data)
{
	struct kwindow kw, *old;
	unsigned long arg;
	size_t len = 0;
	long ret;

	if (lf->f_op->unlocked_ioctl == NULL)
		return ENOTTY;
	if (RUN_ONCE(&kw_once, kw_init) != 0)
		return ENOMEM;
	if (cmd & (IOC_IN | IOC_OUT)) {
		len = IOCPARM_LEN(cmd);
		arg = (unsigned long)(uintptr_t)data;
	} else {
		arg = (unsigned long)*(void **)data;
	}
	kw.kw_ptr = data;
	kw.kw_len = len;
	old = lwp_getspecific(kw_key);
	lwp_setspecific(kw_key, &kw);
	ret = (*lf->f_op->unlocked_ioctl)(lf, (unsigned int)cmd, arg);
	lwp_setspecific(kw_key, old);
	if (ret < 0) {
		if (ret == -ERESTARTSYS)
			return ERESTART;
		return (int)-ret;
	}
	return 0;
}

static int
drm618_fo_ioctl(struct file *fp, u_long cmd, void *data)
{

	return drm618_file_ioctl(LF(fp), cmd, data);
}

static int
drm618_fo_poll(struct file *fp, int events)
{
	struct linux_file *lf = LF(fp);
	struct poll_table_struct pt = { .pt_events = events };
	unsigned int revents;

	if (lf->f_op->poll == NULL)
		return events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM);
	revents = (*lf->f_op->poll)(lf, &pt);
	return revents & (events | POLLERR | POLLHUP);
}

static int
drm618_fo_stat(struct file *fp, struct stat *st)
{
	struct linux_file *lf = LF(fp);

	(void)memset(st, 0, sizeof(*st));
	st->st_dev = lf->f_inode != NULL ? lf->f_inode->i_rdev : NODEV;
	st->st_uid = kauth_cred_geteuid(fp->f_cred);
	st->st_gid = kauth_cred_getegid(fp->f_cred);
	st->st_mode = S_IFCHR;
	st->st_rdev = st->st_dev;
	return 0;
}

static int
drm618_fo_close(struct file *fp)
{
	struct linux_file *lf = LF(fp);

	fp->f_data = NULL;
	lf->f_nbsd = NULL;
	fput(lf);
	return 0;
}

static int
drm618_fo_kqfilter(struct file *fp, struct knote *kn)
{
	struct linux_file *lf = LF(fp);

	/* Only the files that have a kqfilter of their own (a dma-buf) offer it. */
	if (lf->f_op->bsd_kqfilter == NULL)
		return EOPNOTSUPP;
	return (*lf->f_op->bsd_kqfilter)(lf, kn);
}

/* lseek(2) is the llseek of the Linux file; the position is the one NetBSD keeps. */
static int
drm618_fo_seek(struct file *fp, off_t delta, int whence, off_t *newoffp,
    int flags)
{
	struct linux_file *lf = LF(fp);
	loff_t pos;

	if (lf->f_op->llseek == NULL)
		return ESPIPE;
	lf->f_pos = fp->f_offset;
	pos = (*lf->f_op->llseek)(lf, delta, whence);
	if (pos < 0)
		return (int)-pos;
	if (newoffp != NULL)
		*newoffp = pos;
	if (flags & FOF_UPDATE_OFFSET)
		fp->f_offset = pos;
	return 0;
}

static int
drm618_fo_mmap(struct file *fp, off_t *offp, size_t len, int prot, int *flagsp,
    int *advicep, struct uvm_object **uobjp, int *maxprotp)
{

	return drm618_vma_mmap(LF(fp), offp, len, prot, flagsp, advicep, uobjp,
	    maxprotp);
}

static const struct fileops drm618_fileops = {
	.fo_name = "drm618",
	.fo_read = drm618_fo_read,
	.fo_write = fbadop_write,
	.fo_ioctl = drm618_fo_ioctl,
	.fo_fcntl = fnullop_fcntl,
	.fo_poll = drm618_fo_poll,
	.fo_stat = drm618_fo_stat,
	.fo_close = drm618_fo_close,
	.fo_kqfilter = drm618_fo_kqfilter,
	.fo_restart = fnullop_restart,
	.fo_mmap = drm618_fo_mmap,
	.fo_seek = drm618_fo_seek,
};

/* ---- descriptors ---------------------------------------------------- */

struct pending_fd {
	LIST_ENTRY(pending_fd)	pf_entry;
	struct proc		*pf_proc;
	int			pf_fd;
	struct file		*pf_fp;
};

static kmutex_t pending_lock;
static LIST_HEAD(, pending_fd) pending_list;
static ONCE_DECL(pending_once);

static int
pending_init(void)
{

	mutex_init(&pending_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static struct pending_fd *
pending_take(int fd)
{
	struct pending_fd *pf;

	mutex_enter(&pending_lock);
	LIST_FOREACH(pf, &pending_list, pf_entry) {
		if (pf->pf_proc == curproc && pf->pf_fd == fd) {
			LIST_REMOVE(pf, pf_entry);
			break;
		}
	}
	mutex_exit(&pending_lock);
	return pf;
}

int
get_unused_fd_flags(unsigned int flags)
{
	struct pending_fd *pf;
	struct file *fp;
	int fd, error;

	(void)RUN_ONCE(&pending_once, pending_init);
	error = fd_allocfile(&fp, &fd);
	if (error)
		return -error;
	if (flags & O_CLOEXEC)
		fd_set_exclose(curlwp, fd, true);
	pf = kmem_alloc(sizeof(*pf), KM_SLEEP);
	pf->pf_proc = curproc;
	pf->pf_fd = fd;
	pf->pf_fp = fp;
	mutex_enter(&pending_lock);
	LIST_INSERT_HEAD(&pending_list, pf, pf_entry);
	mutex_exit(&pending_lock);
	return fd;
}

void
put_unused_fd(unsigned int fd)
{
	struct pending_fd *pf = pending_take(fd);

	KASSERTMSG(pf != NULL, "put_unused_fd(%u): not reserved", fd);
	if (pf == NULL)
		return;
	fd_abort(curproc, pf->pf_fp, pf->pf_fd);
	kmem_free(pf, sizeof(*pf));
}

void
fd_install(unsigned int fd, struct linux_file *lf)
{
	struct pending_fd *pf = pending_take(fd);
	struct file *fp;

	KASSERTMSG(pf != NULL, "fd_install(%u): not reserved", fd);
	if (pf == NULL)
		return;
	fp = pf->pf_fp;
	kmem_free(pf, sizeof(*pf));
	fp->f_flag = FREAD | FWRITE | (lf->f_flags & O_NONBLOCK ? FNONBLOCK : 0);
	fp->f_type = DTYPE_MISC;
	fp->f_ops = &drm618_fileops;
	fp->f_data = lf;
	lf->f_nbsd = fp;
	fd_affix(curproc, fp, fd);
}

struct linux_file *
fget(unsigned int fd)
{
	struct file *fp = fd_getfile(fd);
	struct linux_file *lf = NULL;

	if (fp == NULL)
		return NULL;
	if (fp->f_ops == &drm618_fileops) {
		lf = LF(fp);
		atomic_inc_uint(&lf->f_refcnt);
	}
	fd_putfile(fd);
	return lf;
}

struct linux_file *
drm618_fget(unsigned int fd, int *error)
{
	struct file *fp = fd_getfile(fd);
	struct linux_file *lf = NULL;

	if (fp == NULL) {
		*error = EBADF;
		return NULL;
	}
	if (fp->f_ops == &drm618_fileops) {
		lf = LF(fp);
		atomic_inc_uint(&lf->f_refcnt);
	} else {
		*error = EINVAL;
	}
	fd_putfile(fd);
	return lf;
}

/* ---- character device --------------------------------------------- */

static const struct file_operations *stub_fops;
static kmutex_t stub_lock;
static ONCE_DECL(stub_once);

static int
stub_init(void)
{

	mutex_init(&stub_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static dev_type_open(drm618_cd_open);

static const struct cdevsw drm618_cdevsw = {
	.d_open = drm618_cd_open,
	.d_close = noclose,
	.d_read = noread,
	.d_write = nowrite,
	.d_ioctl = noioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_NEGOFFSAFE,
};

static int
drm618_cd_open(dev_t dev, int flags, int fmt __unused, struct lwp *l __unused)
{
	const struct file_operations *fops;
	struct linux_file *lf;
	struct file *fp;
	unsigned int lflags;
	int fd, error;

	mutex_enter(&stub_lock);
	fops = stub_fops;
	mutex_exit(&stub_lock);
	if (fops == NULL)
		return ENXIO;
	if (flags & O_EXCL)
		return EBUSY;
	lflags = ((flags & (FREAD | FWRITE)) == FREAD ? O_RDONLY :
	    (flags & (FREAD | FWRITE)) == FWRITE ? O_WRONLY : O_RDWR) |
	    (flags & FNONBLOCK ? O_NONBLOCK : 0);
	error = fd_allocfile(&fp, &fd);
	if (error)
		return error;
	error = drm618_file_open(fops, dev, lflags, &lf);
	if (error) {
		fd_abort(curproc, fp, fd);
		if (error == -ERESTARTSYS)
			return ERESTART;
		return -error;
	}
	lf->f_nbsd = fp;
	error = fd_clone(fp, fd, flags, &drm618_fileops, lf);
	KASSERT(error == EMOVEFD);
	return error;
}

int
register_chrdev(unsigned int major __unused, const char *name,
    const struct file_operations *fops)
{
	devmajor_t bmajor = NODEVMAJOR, cmajor = NODEVMAJOR;
	int error;

	(void)RUN_ONCE(&stub_once, stub_init);
	mutex_enter(&stub_lock);
	if (stub_fops != NULL) {
		mutex_exit(&stub_lock);
		return -EBUSY;
	}
	error = devsw_attach(name, NULL, &bmajor, &drm618_cdevsw, &cmajor);
	if (error == 0) {
		stub_fops = fops;
	}
	mutex_exit(&stub_lock);
	return error ? -error : cmajor;
}

void
unregister_chrdev(unsigned int major __unused, const char *name __unused)
{

	(void)RUN_ONCE(&stub_once, stub_init);
	mutex_enter(&stub_lock);
	if (stub_fops != NULL) {
		devsw_detach(NULL, &drm618_cdevsw);
		stub_fops = NULL;
	}
	mutex_exit(&stub_lock);
}

/* ---- pseudo file system ---------------------------------------------- */

static kmutex_t pseudofs_lock;
static ONCE_DECL(pseudofs_once);

static int
pseudofs_init(void)
{

	mutex_init(&pseudofs_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

struct pseudo_fs_context *
init_pseudo(struct fs_context *fc, unsigned long magic)
{

	fc->magic = magic;
	return (struct pseudo_fs_context *)fc;	/* the context is its first member */
}

void
kill_anon_super(struct super_block *sb __unused)
{

	/* The super block is freed by simple_release_fs. */
}

int
simple_pin_fs(struct file_system_type *type, struct vfsmount **mntp, int *countp)
{
	struct fs_context fc = { .fs_type = type };
	struct vfsmount *mnt;
	int error;

	(void)RUN_ONCE(&pseudofs_once, pseudofs_init);
	mutex_enter(&pseudofs_lock);
	if (*mntp == NULL) {
		error = (*type->init_fs_context)(&fc);
		if (error) {
			mutex_exit(&pseudofs_lock);
			return error;
		}
		mnt = kmem_zalloc(sizeof(*mnt), KM_SLEEP);
		mnt->mnt_sb = kmem_zalloc(sizeof(*mnt->mnt_sb), KM_SLEEP);
		mnt->mnt_sb->s_magic = fc.magic;
		*mntp = mnt;
	}
	(*countp)++;
	mutex_exit(&pseudofs_lock);
	return 0;
}

void
simple_release_fs(struct vfsmount **mntp, int *countp)
{
	struct vfsmount *mnt = NULL;
	struct super_block *sb = NULL;

	mutex_enter(&pseudofs_lock);
	KASSERT(*countp > 0);
	if (--(*countp) == 0) {
		mnt = *mntp;
		*mntp = NULL;
		sb = mnt->mnt_sb;
	}
	mutex_exit(&pseudofs_lock);
	if (mnt != NULL) {
		kmem_free(sb, sizeof(*sb));
		kmem_free(mnt, sizeof(*mnt));
	}
}

struct inode *
alloc_anon_inode(struct super_block *sb)
{
	struct inode *inode = inode_alloc(NODEV, 0);

	inode->i_sb = sb;
	return inode;
}

void
iput(struct inode *inode)
{

	if (inode == NULL)
		return;
	if (atomic_dec_uint_nv(&inode->i_refcnt) == 0)
		kmem_free(inode, sizeof(*inode));
}

/*
 * Before the module goes: the key of the ioctl window, the locks.  EBUSY while
 * the character device is registered or a descriptor is reserved.
 */
int
drm618_file_fini(void)
{

	if (stub_once.o_status) {
		mutex_enter(&stub_lock);
		if (stub_fops != NULL) {
			mutex_exit(&stub_lock);
			return EBUSY;
		}
		mutex_exit(&stub_lock);
	}
	if (pending_once.o_status) {
		mutex_enter(&pending_lock);
		if (!LIST_EMPTY(&pending_list)) {
			mutex_exit(&pending_lock);
			return EBUSY;
		}
		mutex_exit(&pending_lock);
	}
	if (kw_once.o_status)
		lwp_specific_key_delete(kw_key);
	if (stub_once.o_status)
		mutex_destroy(&stub_lock);
	if (pending_once.o_status)
		mutex_destroy(&pending_lock);
	if (pseudofs_once.o_status)
		mutex_destroy(&pseudofs_lock);
	return 0;
}
