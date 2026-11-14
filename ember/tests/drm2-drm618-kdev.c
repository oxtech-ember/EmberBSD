/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the class devices (drm618/drm618_sysfs.c,
 * drm618_kdev.h), the pseudo file system and the small layer functions the
 * 6.18 core needs for its device nodes.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/kdev_t.h>
#include <linux/mutex.h>

#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>

#include "../../sys/external/bsd/drm2/dist618/drm/drm_internal.h"

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static int t_fs_init(struct fs_context *fc)
{

	return init_pseudo(fc, 0x010203ff) ? 0 : -ENOMEM;
}

static struct file_system_type t_fs_type = {
	.name = "t",
	.init_fs_context = t_fs_init,
	.kill_sb = kill_anon_super,
};

int	drm2_drm618_kdev_run(void);

int
drm2_drm618_kdev_run(void)
{
	struct device *kdev, *k2;
	struct drm_minor minor;
	struct drm_device *dev;
	struct drm_connector *conn;
	struct vfsmount *mnt = NULL, *mnt2;
	struct inode *inode;
	int cnt = 0, data = 7;
	atomic_t at;
	struct mutex mu;

	t_run = t_fail = 0;

	/* a class device */
	kdev = drm618_kdev_create(MKDEV(226, 5), &data, "card%d", 5);
	CHECK(kdev != NULL);
	CHECK(drm618_kdev_devt(kdev) == MKDEV(226, 5));
	CHECK(strcmp(drm618_kdev_name(kdev), "card5") == 0);
	CHECK(dev_get_drvdata(kdev) == &data);
	CHECK(!device_is_registered(kdev));
	CHECK(device_add(kdev) == 0);
	CHECK(device_is_registered(kdev));
	device_del(kdev);
	CHECK(!device_is_registered(kdev));
	dev_set_drvdata(kdev, NULL);
	CHECK(dev_get_drvdata(kdev) == NULL);
	CHECK(drm618_kdev_uevent(kdev, NULL) == 0);
	CHECK(drm618_kdev_create_link(kdev, "controlD64") == 0);
	drm618_kdev_remove_link(kdev, "controlD64");
	drm618_kdev_put(kdev);

	/* the class device of a minor, by type */
	CHECK(drm_sysfs_init() == 0);
	memset(&minor, 0, sizeof(minor));
	minor.type = DRM_MINOR_PRIMARY;
	minor.index = 3;
	k2 = drm_sysfs_minor_alloc(&minor);
	CHECK(strcmp(drm618_kdev_name(k2), "card3") == 0);
	CHECK(drm618_kdev_devt(k2) == MKDEV(DRM_MAJOR, 3));
	drm618_kdev_put(k2);
	minor.type = DRM_MINOR_RENDER;
	minor.index = 130;
	k2 = drm_sysfs_minor_alloc(&minor);
	CHECK(strcmp(drm618_kdev_name(k2), "renderD130") == 0);
	drm618_kdev_put(k2);
	minor.type = DRM_MINOR_ACCEL;
	minor.index = 0;
	k2 = drm_sysfs_minor_alloc(&minor);
	CHECK(strcmp(drm618_kdev_name(k2), "accel0") == 0);
	drm618_kdev_put(k2);

	/* the class device of a connector */
	dev = kmem_zalloc(sizeof(*dev), KM_SLEEP);
	conn = kmem_zalloc(sizeof(*conn), KM_SLEEP);
	minor.type = DRM_MINOR_PRIMARY;
	minor.index = 2;
	dev->primary = &minor;
	conn->dev = dev;
	conn->name = __UNCONST("HDMI-A-1");
	CHECK(drm_sysfs_connector_add(conn) == 0);
	CHECK(conn->kdev != NULL);
	CHECK(strcmp(drm618_kdev_name(conn->kdev), "card2-HDMI-A-1") == 0);
	CHECK(drm_sysfs_connector_add_late(conn) == 0);
	drm_sysfs_connector_remove_early(conn);
	drm_sysfs_connector_remove(conn);
	CHECK(conn->kdev == NULL);
	drm_sysfs_connector_remove(conn);	/* twice: harmless */
	kmem_free(conn, sizeof(*conn));
	kmem_free(dev, sizeof(*dev));

	/* the pseudo file system: shared mount, anonymous inode with a mapping */
	CHECK(simple_pin_fs(&t_fs_type, &mnt, &cnt) == 0);
	CHECK(mnt != NULL && mnt->mnt_sb->s_magic == 0x010203ff);
	mnt2 = mnt;
	CHECK(simple_pin_fs(&t_fs_type, &mnt, &cnt) == 0);
	CHECK(mnt == mnt2 && cnt == 2);
	inode = alloc_anon_inode(mnt->mnt_sb);
	CHECK(inode != NULL && inode->i_mapping != NULL && inode->i_sb == mnt->mnt_sb);
	iput(inode);
	simple_release_fs(&mnt, &cnt);
	CHECK(mnt == mnt2 && cnt == 1);
	simple_release_fs(&mnt, &cnt);
	CHECK(mnt == NULL && cnt == 0);

	/* atomic_dec_and_mutex_lock: the mutex is held only by the last one out */
	mutex_init(&mu);
	atomic_set(&at, 2);
	CHECK(!atomic_dec_and_mutex_lock(&at, &mu));
	CHECK(atomic_read(&at) == 1 && !mutex_is_locked(&mu));
	CHECK(atomic_dec_and_mutex_lock(&at, &mu));
	CHECK(atomic_read(&at) == 0 && mutex_is_locked(&mu));
	mutex_unlock(&mu);
	mutex_destroy(&mu);

	if (t_fail != 0)
		printf("drm2-drm618-kdev: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-drm618-kdev: %d checks, 0 failed\n", t_run);
	return t_fail;
}
