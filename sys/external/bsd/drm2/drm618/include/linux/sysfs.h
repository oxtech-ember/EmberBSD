/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of the sysfs interface in dist618/drm/i915
 * (gt/intel_gt_sysfs*.c, i915_sysfs.c, i915_pmu.c, i915_perf.c, i915_hwmon.c, display): the types
 * with their members and the calls; the GPL original was not read.
 *
 * There is no sysfs here.  The types are the shapes the sources fill in; the
 * functions that make nodes are declared and not defined (no 6.18 module
 * that calls them links; see the README, declared only).  sysfs_emit and
 * sysfs_emit_at have behaviour and are written out.
 */
#ifndef _DRM618_LINUX_SYSFS_H_
#define _DRM618_LINUX_SYSFS_H_

#include <sys/types.h>
#include <sys/stdarg.h>

#include <linux/types.h>
#include <linux/compiler.h>

#ifndef _DRM618_UMODE_T
#define _DRM618_UMODE_T
typedef unsigned short umode_t;
#endif

#ifndef S_IRUGO
#define	S_IRUGO	0444
#endif
#ifndef S_IWUSR
#define	S_IWUSR	0200
#endif
#ifndef S_IRUSR
#define	S_IRUSR	0400
#endif

struct kobject;
struct file;

struct attribute {
	const char	*name;
	umode_t		mode;
};

struct bin_attribute {
	struct attribute attr;
	size_t		size;
	void		*private;
	ssize_t		(*read)(struct file *, struct kobject *,
			    const struct bin_attribute *, char *, off_t,
			    size_t);
	ssize_t		(*write)(struct file *, struct kobject *,
			    const struct bin_attribute *, char *, off_t,
			    size_t);
	int		(*mmap)(struct file *, struct kobject *,
			    const struct bin_attribute *, struct vm_area_struct *);
};
struct vm_area_struct;

struct attribute_group {
	const char	*name;
	umode_t		(*is_visible)(struct kobject *, struct attribute *, int);
	umode_t		(*is_bin_visible)(struct kobject *,
			    const struct bin_attribute *, int);
	struct attribute **attrs;
	const struct bin_attribute *const *bin_attrs;
};

struct kobj_attribute {
	struct attribute attr;
	ssize_t		(*show)(struct kobject *, struct kobj_attribute *,
			    char *);
	ssize_t		(*store)(struct kobject *, struct kobj_attribute *,
			    const char *, size_t);
};

struct sysfs_ops {
	ssize_t		(*show)(struct kobject *, struct attribute *, char *);
	ssize_t		(*store)(struct kobject *, struct attribute *,
			    const char *, size_t);
};

#define	__ATTR(_name, _mode, _show, _store) {				\
	.attr = { .name = #_name, .mode = (_mode) },			\
	.show = (_show),						\
	.store = (_store),						\
}
#define	__ATTR_RO(_name)	__ATTR(_name, 0444, _name##_show, NULL)
#define	__ATTR_RW(_name)	__ATTR(_name, 0644, _name##_show, _name##_store)
#define	__ATTR_RO_MODE(_name, _mode)	__ATTR(_name, _mode, _name##_show, NULL)
#define	__ATTR_WO(_name)	__ATTR(_name, 0200, NULL, _name##_store)
#define	__ATTR_NULL		{ .attr = { .name = NULL } }
#define	sysfs_attr_init(attr)	do { } while (0)
#define	ATTRIBUTE_GROUPS(_name)						\
	static const struct attribute_group _name##_group = {		\
		.attrs = _name##_attrs,					\
	};								\
	static const struct attribute_group *_name##_groups[] = {	\
		&_name##_group,						\
		NULL,							\
	}

int	sysfs_create_group(struct kobject *, const struct attribute_group *);
void	sysfs_remove_group(struct kobject *, const struct attribute_group *);
int	sysfs_merge_group(struct kobject *, const struct attribute_group *);
int	sysfs_create_file(struct kobject *, const struct attribute *);
int	sysfs_create_files(struct kobject *, const struct attribute * const *);
void	sysfs_remove_file(struct kobject *, const struct attribute *);
void	sysfs_remove_files(struct kobject *, const struct attribute * const *);
int	sysfs_create_bin_file(struct kobject *, const struct bin_attribute *);
void	sysfs_remove_bin_file(struct kobject *, const struct bin_attribute *);

/* the text goes into one page, as the node's show() contract says; the result is its length */
static inline int __printflike(2, 3)
sysfs_emit(char *buf, const char *fmt, ...)
{
	va_list ap;
	int len;

	KASSERT(buf != NULL);
	va_start(ap, fmt);
	len = vsnprintf(buf, PAGE_SIZE, fmt, ap);
	va_end(ap);
	return MIN(len, (int)PAGE_SIZE - 1);
}

static inline int __printflike(3, 4)
sysfs_emit_at(char *buf, int at, const char *fmt, ...)
{
	va_list ap;
	int len;

	KASSERT(buf != NULL && at >= 0 && at < (int)PAGE_SIZE);
	va_start(ap, fmt);
	len = vsnprintf(buf + at, PAGE_SIZE - at, fmt, ap);
	va_end(ap);
	return MIN(len, (int)PAGE_SIZE - at - 1);
}

#endif
