/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of the names below in the MIT files of
 * dist618/drm/i915 (arguments, results); the GPL originals were not read.
 *
 * Small pieces of the Linux device interfaces that the i915 driver of 6.18 names and the
 * layer lacks.  What is declared and not defined stays so until the device is attached
 * (PCI, interrupts, the root port, CEC, D3cold): a module that calls it does not link.
 */
#ifndef _I915DRM618_DEV_H_
#define _I915DRM618_DEV_H_

#include <linux/dmi.h>
#include <linux/pci.h>
#include <linux/xarray.h>
#include <linux/bitmap.h>
#include <linux/shmem_fs.h>
#include <linux/mm.h>
#include <linux/shrinker.h>
#include <linux/scatterlist.h>
#include <linux/uio.h>

/* The product SKU of the SMBIOS: not a field the layer's dmi_check_system knows; it reports an unknown field and does not match. */
#ifndef DMI_PRODUCT_SKU
#define	DMI_PRODUCT_SKU		(DMI_STRING_MAX + 1)
#endif

/* The switch of a static key on (the layer's key is a bool). */
static inline void
static_branch_enable(bool *key)
{

	*key = true;
}

static inline void
static_branch_disable(bool *key)
{

	*key = false;
}

/* PCI (declared, not defined) */
struct pci_dev *pcie_find_root_port(struct pci_dev *);
void	pci_d3cold_disable(struct pci_dev *);
void	pci_d3cold_enable(struct pci_dev *);
struct pci_bus;
void	pci_assign_unassigned_bus_resources(struct pci_bus *);

/* CEC: the notifier of a connector (declared, not defined: there is no CEC) */
struct cec_notifier;
struct cec_connector_info;
struct cec_notifier *cec_notifier_conn_register(struct device *, const char *,
    const struct cec_connector_info *);
void	cec_notifier_conn_unregister(struct cec_notifier *);
void	cec_notifier_set_phys_addr(struct cec_notifier *, u16);
void	cec_notifier_phys_addr_invalidate(struct cec_notifier *);

/* ---- memory, xarray, bitmap regions, pids, shrinker (tests: drm2-i915-layer-c.c) ---- */

/* xarray value entries: an integer that does not collide with a pointer (bit 0 set) */
static inline void *
xa_mk_value(unsigned long v)
{

	return (void *)((v << 1) | 1);
}

static inline unsigned long
xa_to_value(const void *entry)
{

	return (unsigned long)entry >> 1;
}

static inline bool
xa_is_value(const void *entry)
{

	return ((unsigned long)entry & 1) != 0;
}

/* The lock of a layer xarray is a spin lock at interrupt level; the flag changes nothing. */
#define	XA_FLAGS_LOCK_IRQ	0

/*
 * Allocate an index from *next up to the limit, or from the start of the limit
 * if there is none left; *next goes to the one after.  0 or 1 (wrapped), -EBUSY if the
 * limit is full.
 */
static inline int
xa_alloc_cyclic_irq(struct xarray *xa, uint32_t *id, void *entry,
    struct xa_limit limit, uint32_t *next, gfp_t gfp)
{
	struct xa_limit tail = limit;
	int wrapped = 0, err;

	if (*next < limit.min)
		*next = limit.min;
	tail.min = *next;
	err = xa_alloc(xa, id, entry, tail, gfp);
	if (err == -EBUSY && *next > limit.min) {
		tail.min = limit.min;
		tail.max = *next - 1;
		err = xa_alloc(xa, id, entry, tail, gfp);
		wrapped = 1;
	}
	if (err)
		return err;
	*next = (*id == limit.max) ? limit.min : *id + 1;
	if (*id == limit.max)
		wrapped = 1;
	return wrapped;
}

/* __xa_store and __xa_erase want the caller to hold the lock; the layer's xa_store takes it itself (declared, not defined). */
void	*__xa_store(struct xarray *, unsigned long, void *, gfp_t);
void	*__xa_erase(struct xarray *, unsigned long);

/*
 * A run of 2^order bits, aligned to its size, that is clear: set it and give
 * its first bit, or -ENOMEM.  Clearing the run again.
 */
static inline int
bitmap_find_free_region(unsigned long *bitmap, unsigned int bits, int order)
{
	unsigned int len = 1u << order, pos, i;

	for (pos = 0; pos + len <= bits; pos += len) {
		for (i = 0; i < len; i++)
			if (test_bit(pos + i, bitmap))
				break;
		if (i == len) {
			for (i = 0; i < len; i++)
				__set_bit(pos + i, bitmap);
			return pos;
		}
	}
	return -ENOMEM;
}

static inline void
bitmap_release_region(unsigned long *bitmap, unsigned int pos, int order)
{
	unsigned int len = 1u << order, i;

	for (i = 0; i < len; i++)
		__clear_bit(pos + i, bitmap);
}

/* the contents of a page, through its direct mapping */
static inline void
memcpy_to_page(struct page *page, size_t offset, const char *from, size_t len)
{

	memcpy((char *)page_address(page) + offset, from, len);
}

static inline void
memcpy_from_page(char *to, struct page *page, size_t offset, size_t len)
{

	memcpy(to, (const char *)page_address(page) + offset, len);
}

static inline struct page *
shmem_read_mapping_page(struct address_space *mapping, unsigned long index)
{

	return shmem_read_mapping_page_gfp(mapping, index, mapping_gfp_mask(mapping));
}

/* the page cache of a file: its gfp mask and its range (declared, not defined) */
void	mapping_set_gfp_mask(struct address_space *, gfp_t);
void	shmem_truncate_range(struct inode *, loff_t, loff_t);

/* user memory, the address space of the caller (declared, not defined) */
int	mmap_write_lock_killable(struct mm_struct *);
void	mmap_write_unlock(struct mm_struct *);
void	mmap_read_lock(struct mm_struct *);
void	mmap_read_unlock(struct mm_struct *);
struct vm_area_struct *find_vma(struct mm_struct *, unsigned long);
struct vma_iterator {
	struct mm_struct	*mm;
	unsigned long		addr;
};
#define	VMA_ITERATOR(name, _mm, _addr)					\
	struct vma_iterator name = { .mm = (_mm), .addr = (_addr) }
struct vm_area_struct *vma_find(struct vma_iterator *, unsigned long);
#define	for_each_vma_range(vmi, vma, end)				\
	while (((vma) = vma_find(&(vmi), (end))) != NULL)
struct file;
void	vma_set_file(struct vm_area_struct *, struct file *);
struct file *get_file_active(struct file **);
int	vfs_mmap(struct file *, struct vm_area_struct *);
void	pagefault_disable(void);
void	pagefault_enable(void);
unsigned long copy_from_user_inatomic_nontemporal(void *, const void __user *, unsigned long);
void	*vmap_pfn(unsigned long *, unsigned int, pgprot_t);
#define	PAGE_KERNEL_IO		(VM_PROT_READ | VM_PROT_WRITE)

/* the shrinker of 6.18: allocated, registered and freed; the shared layer's struct is for the 5.6 drivers */
struct shrinker *shrinker_alloc(unsigned int, const char *, ...);
void	shrinker_register(struct shrinker *);
void	shrinker_free(struct shrinker *);

/* module parameters with functions of their own (the parameter is not made: nothing reads it here) */
struct kernel_param;
struct kernel_param_ops {
	int	(*set)(const char *, const struct kernel_param *);
	int	(*get)(char *, const struct kernel_param *);
	void	(*free)(void *);
};
struct kernel_param {
	const char			*name;
	const struct kernel_param_ops	*ops;
	void				*arg;
	unsigned int			perm;
};
#define	module_param_cb_unsafe(name, ops, arg, perm)			\
	static const struct kernel_param __param_cb_##name __unused =	\
	    { #name, (ops), (arg), (perm) }
#define	module_param_cb(name, ops, arg, perm)	module_param_cb_unsafe(name, ops, arg, perm)

/* ---- the PCI driver, sysctl, the device attribute, VGA, DMA mapping (declared, not defined: phase 2) ---- */

#include <linux/dma-fence-array.h>
#include <linux/sysfs.h>

struct dev_pm_ops;
struct pci_driver {
	const char			*name;
	const struct pci_device_id	*id_table;
	int	(*probe)(struct pci_dev *, const struct pci_device_id *);
	void	(*remove)(struct pci_dev *);
	void	(*shutdown)(struct pci_dev *);
	struct {
		const struct dev_pm_ops	*pm;
	}				driver;
};
int	pci_register_driver(struct pci_driver *);
void	pci_unregister_driver(struct pci_driver *);

/* the interrupt line of a PCI device: the shared struct pci_dev does not keep it */
int	drm618_pci_irq(struct pci_dev *);

/* the xarray without the lock held by the caller: the layer's erase takes it itself */
#define	xa_erase_irq(xa, index)	xa_erase((xa), (index))

/* the timer's next expiry and the mark of a cancelled timer (declared, not defined: the layer's timer_list has no expires) */
void	drm618_timer_clear_expires(struct timer_list *);

/* sysctl tables of a driver */
struct ctl_table_header;
struct ctl_table {
	const char	*procname;
	void		*data;
	int		maxlen;
	umode_t		mode;
	int		(*proc_handler)(const struct ctl_table *, int, void *, size_t *, loff_t *);
	void		*extra1;
	void		*extra2;
};
extern const int drm618_sysctl_zero, drm618_sysctl_one;
#define	SYSCTL_ZERO	((void *)&drm618_sysctl_zero)
#define	SYSCTL_ONE	((void *)&drm618_sysctl_one)
int	proc_dointvec_minmax(const struct ctl_table *, int, void *, size_t *, loff_t *);
struct ctl_table_header *register_sysctl(const char *, const struct ctl_table *);
void	unregister_sysctl_table(struct ctl_table_header *);

/* a file of the kernel with no inode in the file system */
int	anon_inode_getfd(const char *, const struct file_operations *, void *, int);

/* the attributes of a device (sysfs) */
struct device_attribute {
	struct attribute attr;
	ssize_t	(*show)(struct device *, struct device_attribute *, char *);
	ssize_t	(*store)(struct device *, struct device_attribute *, const char *, size_t);
};
#ifndef DEVICE_ATTR_RO
#define	DEVICE_ATTR(_name, _mode, _show, _store)			\
	struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, _show, _store)
#define	DEVICE_ATTR_RO(_name)	struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#define	DEVICE_ATTR_RW(_name)	struct device_attribute dev_attr_##_name = __ATTR_RW(_name)
#define	DEVICE_ATTR_WO(_name)	struct device_attribute dev_attr_##_name = __ATTR_WO(_name)
#endif
int	device_create_bin_file(struct device *, const struct bin_attribute *);
void	device_remove_bin_file(struct device *, const struct bin_attribute *);

/* VGA arbitration and the legacy ports */
int	vga_get_uninterruptible(struct pci_dev *, unsigned int);
void	vga_client_unregister(struct pci_dev *);

static inline uint8_t
inb(uint16_t port)
{
	uint8_t v;

	__asm volatile("inb %1,%0" : "=a"(v) : "Nd"(port));
	return v;
}

static inline void
outb(uint8_t v, uint16_t port)
{

	__asm volatile("outb %0,%1" : : "a"(v), "Nd"(port));
}

/* DMA mapping of a scatter list (declared, not defined: no device DMA tag yet) */
#ifndef DMA_ATTR_NO_KERNEL_MAPPING
#define	DMA_ATTR_NO_KERNEL_MAPPING	(1UL << 4)
#endif
int	dma_map_sg_attrs(struct device *, struct scatterlist *, int, int, unsigned long);
void	dma_unmap_sg(struct device *, struct scatterlist *, int, int);

/* writing to a file of the kernel; the buffer of an iterator over user memory */
struct file;
ssize_t	kernel_write(struct file *, const void *, size_t, loff_t *);

/* the name of the power group of a device in sysfs, "power" (declared, not defined: no sysfs) */
extern const char power_group_name[];

/* the writeback of the pages of a shmem file (declared, not defined: the swap out of these pages is the pager's) */
enum writeback_sync_modes { WB_SYNC_NONE, WB_SYNC_ALL };
#define	SWAP_CLUSTER_MAX	32UL
struct writeback_control {
	enum writeback_sync_modes	sync_mode;
	long				nr_to_write;
	loff_t				range_start;
	loff_t				range_end;
};
struct folio *writeback_iter(struct address_space *, struct writeback_control *, struct folio *, int *);
bool	folio_mapped(struct folio *);
void	folio_redirty_for_writepage(struct writeback_control *, struct folio *);
struct swap_iocb;
int	shmem_writeout(struct folio *, struct swap_iocb **, struct list_head *);

/* one write of a file through an iterator: the control block of the call */
struct kiocb {
	struct file	*ki_filp;
	loff_t		ki_pos;
};
#define	MAX_RW_COUNT	(INT_MAX & ~(PAGE_SIZE - 1))
static inline void
init_sync_kiocb(struct kiocb *kiocb, struct file *file)
{

	kiocb->ki_filp = file;
	kiocb->ki_pos = 0;
}

/* a scatter list entry for the pages of a folio */
static inline void
sg_set_folio(struct scatterlist *sg, struct folio *folio, size_t len, size_t offset)
{

	sg_set_page(sg, &folio->page, len, offset);
}

/* x86-64 Linux values for the large file checks and the queued I/O result of the shmem write */
#ifndef EIOCBQUEUED
#define	EIOCBQUEUED		529
#endif
#define	MAX_LFS_FILESIZE	((loff_t)LLONG_MAX)
#define	O_LARGEFILE		0
#define	force_o_largefile()	(sizeof(long) != 4)

/* relay: a channel of sub buffers to a user space reader (declared, not defined: no relay, no debugfs) */
struct rchan_buf;
struct rchan {
	size_t	subbuf_size;	/* the size of one sub buffer */
	size_t	n_subbufs;
	void	*private_data;
};
struct dentry;
struct rchan_callbacks {
	int	(*subbuf_start)(struct rchan_buf *, void *, void *);
	struct dentry *(*create_buf_file)(const char *, struct dentry *, umode_t, struct rchan_buf *, int *);
	int	(*remove_buf_file)(struct dentry *);
};
bool	relay_buf_full(struct rchan_buf *);
void	*relay_reserve(struct rchan *, size_t);
void	relay_flush(struct rchan *);
struct rchan *relay_open(const char *, struct dentry *, size_t, size_t, const struct rchan_callbacks *, void *);
void	relay_close(struct rchan *);
extern const struct file_operations relay_file_operations;

/* a page for the driver's own use (declared, not defined: the allocation of pages is phase 2) */
unsigned long __get_free_page(gfp_t);
void	free_page(unsigned long);

/* a time in milliseconds as a ktime (nanoseconds); the number is not range checked, as Linux's */
static inline ktime_t
ms_to_ktime(u64 ms)
{

	return (ktime_t)(ms * 1000000ull);
}

/* "package not installed": the layer's own number (Linux's collides with a NetBSD one) */
#ifndef ENOPKG
#define	ENOPKG	523
#endif

/* NetBSD's autoconfiguration has a QUIET flag; i915 defines its own */
#include <sys/device.h>
#undef	QUIET

/* The i915 files name their table of operations in debugfs_create_file: it stays referenced (the DRM core defines none) */
#include <linux/debugfs.h>
#undef	debugfs_create_file
#define	debugfs_create_file(name, mode, parent, data, fops)		\
	((void)(name), (void)(mode), (void)(parent), (void)(data), (void)(fops), \
	    (struct dentry *)NULL)

#endif
