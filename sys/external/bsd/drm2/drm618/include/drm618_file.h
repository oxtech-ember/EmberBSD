/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface the
 * 6.18 DRM sources use (struct file, struct inode, struct file_operations,
 * the descriptor calls), no Linux code.
 *
 * The 6.18 core is written for Linux files: it keeps its drm_file in
 * file->private_data, replaces file->f_op at open, reads file->f_flags and
 * hands out descriptors with get_unused_fd_flags/fd_install.  Here a Linux
 * file is a small object of the layer.  The cdevsw of the core (see
 * drm618_file.c) makes one when a device node is opened, calls
 * f_op->open, and hangs it on the NetBSD file (fd_clone) as f_data; the
 * file operations of NetBSD (read, ioctl, poll, close) call the Linux ones.
 *
 * NetBSD's struct file (<sys/file.h>) has the same tag.  Sources of the 6.18
 * core never see it: drm618_prefix.h keeps <sys/file.h> out, and struct file
 * is the Linux object.  The one source that needs both (drm618_file.c) is
 * built with -DDRM618_NBSD_FILE; there the Linux object is struct linux_file.
 */

#ifndef _DRM618_FILE_H_
#define _DRM618_FILE_H_

#include <sys/types.h>
#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/atomic.h>
#include <sys/uio.h>

#include <linux/kdev_t.h>

#ifdef DRM618_NBSD_FILE
#define	DRM618_FILE_TAG	linux_file
#else
#define	DRM618_FILE_TAG	file
#endif

#ifndef __user
#define	__user
#endif

struct DRM618_FILE_TAG;
struct inode;
struct vm_area_struct;
struct poll_table_struct;
struct seq_file;
struct super_block;
struct uvm_object;

typedef off_t	loff_t;
typedef unsigned int	fmode_t;

/*
 * struct address_space of a file: the page cache of the file.  Only the
 * object that holds the pages is here (a uvm_object for shared memory files,
 * NULL for the anonymous inode of a drm_device, which has no pages of its own).
 */
struct address_space {
	struct uvm_object	*a_uobj;
};

struct inode {
	dev_t			i_rdev;
	mode_t			i_mode;
	loff_t			i_size;
	struct address_space	*i_mapping;
	struct address_space	i_data;
	struct super_block	*i_sb;
	void			*i_private;
	unsigned		i_refcnt;
};

/* The Linux number of a device node is major << 20 | minor; here the minor is the NetBSD minor. */
static inline unsigned int
iminor(const struct inode *inode)
{

	return minor(inode->i_rdev);
}

static inline unsigned int
imajor(const struct inode *inode)
{

	return major(inode->i_rdev);
}

#define	FOP_UNSIGNED_OFFSET	0x0020u	/* the file offset is unsigned (drm_file.c) */
#define	FOP_MMAP_SYNC		0x0001u

struct module;

struct file_operations {
	struct module	*owner;
	unsigned int	fop_flags;
	loff_t		(*llseek)(struct DRM618_FILE_TAG *, loff_t, int);
	ssize_t		(*read)(struct DRM618_FILE_TAG *, char __user *, size_t,
			    loff_t *);
	ssize_t		(*write)(struct DRM618_FILE_TAG *, const char __user *,
			    size_t, loff_t *);
	unsigned int	(*poll)(struct DRM618_FILE_TAG *,
			    struct poll_table_struct *);
	long		(*unlocked_ioctl)(struct DRM618_FILE_TAG *, unsigned int,
			    unsigned long);
	long		(*compat_ioctl)(struct DRM618_FILE_TAG *, unsigned int,
			    unsigned long);
	int		(*mmap)(struct DRM618_FILE_TAG *,
			    struct vm_area_struct *);
	int		(*open)(struct inode *, struct DRM618_FILE_TAG *);
	int		(*release)(struct inode *, struct DRM618_FILE_TAG *);
	void		(*show_fdinfo)(struct seq_file *,
			    struct DRM618_FILE_TAG *);
	unsigned long	(*get_unmapped_area)(struct DRM618_FILE_TAG *,
			    unsigned long, unsigned long, unsigned long,
			    unsigned long);
};

struct DRM618_FILE_TAG {
	volatile unsigned int		f_refcnt;
	void				*private_data;
	const struct file_operations	*f_op;
	unsigned int			f_flags;
	fmode_t				f_mode;
	loff_t				f_pos;
	struct inode			*f_inode;
	struct address_space		*f_mapping;
	bool				f_inode_owned;	/* freed with the file */
	void				*f_nbsd;	/* the NetBSD file it hangs on, or NULL */
};

#define	O_LARGEFILE_LINUX	0
#define	FMODE_READ		0x1u
#define	FMODE_WRITE		0x2u

static inline struct inode *
file_inode(const struct DRM618_FILE_TAG *filp)
{

	return filp->f_inode;
}

/* The operations are static data here: no module reference to take. */
#define	fops_get(fops)		(fops)
#define	fops_put(fops)		((void)(fops))
#define	replace_fops(filp, fops)	((void)((filp)->f_op = (fops)))

loff_t	noop_llseek(struct DRM618_FILE_TAG *, loff_t, int);

/* Poll events as Linux numbers them (the same bits as POLL*). */
#define	EPOLLIN		0x0001u
#define	EPOLLPRI	0x0002u
#define	EPOLLOUT	0x0004u
#define	EPOLLERR	0x0008u
#define	EPOLLHUP	0x0010u
#define	EPOLLRDNORM	0x0040u
#define	EPOLLWRNORM	0x0100u

/*
 * Files and descriptors.  get_unused_fd_flags reserves a descriptor of the
 * calling process, fd_install makes it name a file (the descriptor owns the
 * reference the caller held), put_unused_fd gives an unused one back.
 * fget takes a new reference, fput drops one; the last drop runs
 * f_op->release and frees the file.
 */
struct DRM618_FILE_TAG *drm618_file_alloc(const struct file_operations *,
    unsigned int flags, void *private_data);
struct DRM618_FILE_TAG *anon_inode_getfile(const char *,
    const struct file_operations *, void *, int);
struct DRM618_FILE_TAG *get_file(struct DRM618_FILE_TAG *);
void	fput(struct DRM618_FILE_TAG *);
int	get_unused_fd_flags(unsigned int);
void	put_unused_fd(unsigned int);
void	fd_install(unsigned int, struct DRM618_FILE_TAG *);
struct DRM618_FILE_TAG *fget(unsigned int);

/* The file of a device node opened with fops (what the cdevsw does); 0 or a negative errno. */
int	drm618_file_open(const struct file_operations *, dev_t, unsigned int,
    struct DRM618_FILE_TAG **);
int	drm618_file_ioctl(struct DRM618_FILE_TAG *, unsigned long, void *);

/*
 * Character devices.  register_chrdev(0, name, fops) makes a cdevsw whose
 * open builds a file of the Linux kind for the minor opened, runs
 * fops->open on it and hangs it on the NetBSD file; it returns the
 * major, or a negative errno.  The Linux major is ignored (a NetBSD one is
 * assigned).
 */
int	register_chrdev(unsigned int, const char *, const struct file_operations *);
void	unregister_chrdev(unsigned int, const char *);

/*
 * Run an ioctl of the Linux file operations on data that NetBSD already
 * copied in (drm618_file.c arms the pointer for copy_from_user/copy_to_user,
 * see drm618_copy_from_user).
 */
int	drm618_copy_from_user(void *, const void *, size_t);
int	drm618_copy_to_user(void *, const void *, size_t);

#endif	/* _DRM618_FILE_H_ */
