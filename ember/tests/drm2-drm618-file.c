/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the Linux file model of the 6.18 core
 * (drm618/include/drm618_file.h, drm618_file.c): files, descriptors, the open
 * of a device node, the ioctl data window, device numbers.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ioccom.h>
#include <sys/filedesc.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static int released, opened, last_minor;
static void *released_priv;

static int
t_release(struct inode *inode, struct file *filp)
{

	released++;
	released_priv = filp->private_data;
	return 0;
}

static const struct file_operations t_fops = {
	.release = t_release,
};

static int
t_open(struct inode *inode, struct file *filp)
{

	opened++;
	last_minor = iminor(inode);
	if (last_minor == 99)
		return -ENODEV;
	filp->private_data = &last_minor;
	replace_fops(filp, &t_fops);
	return 0;
}

static const struct file_operations t_stub_fops = {
	.open = t_open,
};

static long
t_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	uint32_t v;

	if (copy_from_user(&v, (void __user *)arg, sizeof(v)))
		return -EFAULT;
	if (v == 0xdead)
		return -EINVAL;
	v += 1;
	if (copy_to_user((void __user *)arg, &v, sizeof(v)))
		return -EFAULT;
	return 0;
}

static long
t_ioctl_nested(struct file *filp, unsigned int cmd, unsigned long arg)
{
	uint32_t v;

	/* a pointer inside the data is a user address: must not be the window */
	if (copy_from_user(&v, (void __user *)(uintptr_t)0x1000, sizeof(v)) == 0)
		return -EPERM;
	return 0;
}

static const struct file_operations t_ioctl_fops = {
	.unlocked_ioctl = t_ioctl,
};
static const struct file_operations t_nested_fops = {
	.unlocked_ioctl = t_ioctl_nested,
};

int	drm2_drm618_file_run(void);

int
drm2_drm618_file_run(void)
{
	struct file *f, *g;
	struct inode *inode;
	int fd, fd2, token = 5;
	uint32_t data;
	char kbuf[4];

	t_run = t_fail = 0;
	released = opened = 0;

	/* an anonymous file: operations, private data, flags, inode, mapping */
	f = anon_inode_getfile("t", &t_fops, &token, O_RDWR);
	CHECK(f != NULL);
	CHECK(f->f_op == &t_fops);
	CHECK(f->private_data == &token);
	CHECK((f->f_flags & O_ACCMODE) == O_RDWR);
	CHECK(file_inode(f) != NULL);
	CHECK(f->f_mapping == file_inode(f)->i_mapping);
	CHECK(f->f_mapping != NULL);

	/* the reference count: release at the last drop only */
	CHECK(get_file(f) == f);
	fput(f);
	CHECK(released == 0);
	fput(f);
	CHECK(released == 1);
	CHECK(released_priv == &token);

	/* a descriptor: reserve, install, look up, close */
	fd = get_unused_fd_flags(O_CLOEXEC);
	CHECK(fd >= 0);
	fd2 = get_unused_fd_flags(0);
	CHECK(fd2 >= 0 && fd2 != fd);
	put_unused_fd(fd2);
	CHECK(get_unused_fd_flags(0) == fd2);	/* the unused one is free again */
	put_unused_fd(fd2);
	released = 0;
	f = anon_inode_getfile("t", &t_fops, &token, O_RDWR);
	fd_install(fd, f);
	g = fget(fd);
	CHECK(g == f);
	CHECK(fget(fd + 1000) == NULL);
	{
		CLASS(fd, d)(fd);
		CHECK(!fd_empty(d));
		CHECK(fd_file(d) == f);
	}
	fput(g);	/* the reference of the fget above; the class dropped its own */
	CHECK(released == 0);
	/* like close(2): the caller holds a descriptor reference */
	CHECK(fd_getfile(fd) != NULL);
	CHECK(fd_close(fd) == 0);
	CHECK(released == 1);
	CHECK(fget(fd) == NULL);

	/* the open of a device node: stub open replaces the operations and sets the private data */
	CHECK(drm618_file_open(&t_stub_fops, makedev(0, 7), O_RDWR, &f) == 0);
	CHECK(opened == 1);
	CHECK(last_minor == 7);
	CHECK(f->f_op == &t_fops);
	CHECK(f->private_data == &last_minor);
	inode = file_inode(f);
	CHECK(iminor(inode) == 7);
	released = 0;
	fput(f);
	CHECK(released == 1);
	/* open that fails: no release */
	released = 0;
	CHECK(drm618_file_open(&t_stub_fops, makedev(0, 99), O_RDWR, &f) == -ENODEV);
	CHECK(released == 0);

	/* ioctl data: NetBSD copied it in, the Linux side copies from the "user" pointer */
	f = anon_inode_getfile("t", &t_ioctl_fops, NULL, O_RDWR);
	data = 41;
	CHECK(drm618_file_ioctl(f, _IOWR('t', 1, uint32_t), &data) == 0);
	CHECK(data == 42);
	data = 0xdead;
	CHECK(drm618_file_ioctl(f, _IOWR('t', 1, uint32_t), &data) == EINVAL);
	CHECK(data == 0xdead);
	/* outside an ioctl a kernel pointer is not a user pointer */
	CHECK(drm618_copy_from_user(kbuf, &data, sizeof(kbuf)) != 0);
	fput(f);
	f = anon_inode_getfile("t", &t_nested_fops, NULL, O_RDWR);
	data = 1;
	CHECK(drm618_file_ioctl(f, _IOWR('t', 2, uint32_t), &data) == 0);
	fput(f);
	/* no ioctl operation */
	f = anon_inode_getfile("t", &t_fops, NULL, O_RDWR);
	CHECK(drm618_file_ioctl(f, _IOWR('t', 1, uint32_t), &data) == ENOTTY);
	fput(f);

	/* device numbers */
	CHECK(old_encode_dev(MKDEV(226, 3)) == ((226u << 8) | 3));
	CHECK(MINORBITS == 20);
	CHECK(noop_llseek(NULL, 5, 0) == 0);

	if (t_fail != 0)
		printf("drm2-drm618-file: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-drm618-file: %d checks, 0 failed\n", t_run);
	return t_fail;
}
