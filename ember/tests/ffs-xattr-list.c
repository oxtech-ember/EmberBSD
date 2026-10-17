/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted actual-source FFS list-format contract. */
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ffs-extattr-layout.h"
#include "ffs-extattr-constants.h"

#define UFS1 1
#define UFS2 2
#define VREG 1
#define VCHR 2
#define VBLK 3
#define VREAD 4
#define VTOI(vp) ((vp)->inode)
#define ALIGNED_TO(ptr, type) (((uintptr_t)(ptr) % _Alignof(type)) == 0)
struct ufsmount { int um_fstype; };
struct inode {
	struct ufsmount *i_ump;
	unsigned char *i_ea_area;
	size_t i_ea_len;
};
struct vnode { struct inode *inode; int v_type; };
struct uio { unsigned char *bytes; size_t used; ssize_t uio_resid; };
struct vop_listextattr_args {
	struct vnode *a_vp;
	int a_attrnamespace;
	struct uio *a_uio;
	size_t *a_size;
	int a_flag;
	int a_cred;
};
static int access_error, open_error, moves, fail_move, opens, closes;
static unsigned checks;
#define CHECK(test) do { checks++; if (!(test)) { \
	fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #test); \
	abort(); \
} } while (0)

static int
extattr_check_cred(struct vnode *vp, int space, int cred, int access)
{
	CHECK(vp != NULL && space > 0 && cred == 7 && access == VREAD);
	return access_error;
}

static int
ffs_open_ea(struct vnode *vp, int cred)
{
	CHECK(vp != NULL && cred == 7);
	opens++;
	return open_error;
}

static void
ffs_close_ea(struct vnode *vp, int commit, int cred)
{
	CHECK(vp != NULL && commit == 0 && cred == 7);
	closes++;
}

#ifdef UFS_EXTATTR
static int
ufs_listextattr(struct vop_listextattr_args *ap)
{
	CHECK(ap->a_flag == EXTATTR_LIST_LENPREFIX);
	return EBUSY;
}
#endif

static int
uiomove(void *from, size_t size, struct uio *uio)
{
	size_t count;

	if (++moves == fail_move)
		return EFAULT;
	CHECK(uio->uio_resid >= 0);
	count = size < (size_t)uio->uio_resid ? size : (size_t)uio->uio_resid;
	memcpy(uio->bytes + uio->used, from, count);
	uio->used += count;
	uio->uio_resid -= (ssize_t)count;
	return 0;
}

#define KASSERT(test) CHECK(test)
#include "ffs-listextattr-body.h"

static _Alignas(struct extattr) unsigned char area[1024];
static struct ufsmount mount_state = { UFS2 };
static struct inode inode_state = { &mount_state, area, 0 };
static struct vnode vnode_state = { &inode_state, VREG };

static void
reset(void)
{
	memset(area, 0x7d, sizeof(area));
	inode_state.i_ea_len = 0;
	mount_state.um_fstype = UFS2;
	vnode_state.v_type = VREG;
	access_error = open_error = moves = fail_move = opens = closes = 0;
}

static void
add_name(int space, const char *name)
{
	struct extattr *entry;
	size_t length, record;

	length = strlen(name);
	CHECK(length > 0 && length <= UINT8_MAX);
	record = (offsetof(struct extattr, ea_name) + length + 7) & ~(size_t)7;
	CHECK(inode_state.i_ea_len + record <= sizeof(area));
	entry = (struct extattr *)(area + inode_state.i_ea_len);
	entry->ea_length = (uint32_t)record;
	entry->ea_namespace = (uint8_t)space;
	entry->ea_namelength = (uint8_t)length;
	memcpy(entry->ea_name, name, length);
	inode_state.i_ea_len += record;
}

static struct vop_listextattr_args
arguments(struct uio *uio, size_t *size, int flags)
{
	struct vop_listextattr_args ap = {
		.a_vp = &vnode_state,
		.a_attrnamespace = EXTATTR_NAMESPACE_USER,
		.a_uio = uio,
		.a_size = size,
		.a_flag = flags,
		.a_cred = 7,
	};

	return ap;
}

static void
format_case(int flags)
{
	unsigned char output[300], expected[300];
	char long_name[256];
	struct uio uio = { output, 0, sizeof(output) };
	struct vop_listextattr_args ap;
	const char *names[2];
	size_t expected_size = 0, reported = 123;
	unsigned int i;

	reset();
	memset(long_name, 'x', sizeof(long_name) - 1);
	long_name[255] = 0;
	names[0] = "user.ember-abi";
	names[1] = long_name;
	add_name(EXTATTR_NAMESPACE_USER, names[0]);
	add_name(EXTATTR_NAMESPACE_SYSTEM, "system.hidden");
	add_name(EXTATTR_NAMESPACE_USER, names[1]);
	for (i = 0; i < 2; i++) {
		size_t length = strlen(names[i]);

		if (flags & EXTATTR_LIST_LENPREFIX)
			expected[expected_size++] = (unsigned char)length;
		memcpy(expected + expected_size, names[i], length);
		expected_size += length;
		if (!(flags & EXTATTR_LIST_LENPREFIX))
			expected[expected_size++] = 0;
	}
	ap = arguments(NULL, &reported, flags);
	CHECK(ffs_listextattr(&ap) == 0 && reported == expected_size);
	CHECK(moves == 0 && opens == 1 && closes == 1);
	memset(output, 0xa5, sizeof(output));
	uio.uio_resid = (ssize_t)reported;
	ap = arguments(&uio, NULL, flags);
	CHECK(ffs_listextattr(&ap) == 0 && uio.used == expected_size);
	CHECK(memcmp(output, expected, expected_size) == 0);
	CHECK(output[expected_size] == 0xa5 && opens == 2 && closes == 2);
}

int
main(void)
{
	unsigned char output[32];
	struct uio uio;
	struct vop_listextattr_args ap;
	size_t size;
	int fault;

	/* The baseline must compile and fail this exact NUL-format assertion. */
	format_case(0);
	format_case(EXTATTR_LIST_LENPREFIX);
	reset();
	size = 123;
	ap = arguments(NULL, &size, 0);
	CHECK(ffs_listextattr(&ap) == 0 && size == 0);
	CHECK(opens == 1 && closes == 1);
	reset();
	add_name(EXTATTR_NAMESPACE_USER, "user.ember-abi");
	memset(output, 0xa5, sizeof(output));
	uio = (struct uio){ output, 0, 14 };
	ap = arguments(&uio, NULL, 0);
	CHECK(ffs_listextattr(&ap) == ERANGE);
	CHECK(uio.used == 0 && output[0] == 0xa5 && closes == 1);
	/* BSD callers retain the existing read-style truncation contract. */
	uio = (struct uio){ output, 0, 3 };
	ap = arguments(&uio, NULL, EXTATTR_LIST_LENPREFIX);
	CHECK(ffs_listextattr(&ap) == 0 && uio.used == 3);
	CHECK(output[0] == 14 && output[1] == 'u' && output[2] == 's');
	/* A complete first name cannot hide a truncated second name. */
	reset();
	add_name(EXTATTR_NAMESPACE_USER, "one");
	add_name(EXTATTR_NAMESPACE_USER, "two");
	memset(output, 0xa5, sizeof(output));
	uio = (struct uio){ output, 0, 7 };
	ap = arguments(&uio, NULL, 0);
	CHECK(ffs_listextattr(&ap) == ERANGE && closes == 1);
	CHECK(uio.used == 4 && memcmp(output, "one", 4) == 0);
	CHECK(output[4] == 0xa5);
	for (fault = 1; fault <= 2; fault++) {
		reset();
		add_name(EXTATTR_NAMESPACE_USER, "one");
		uio = (struct uio){ output, 0, sizeof(output) };
		ap = arguments(&uio, NULL, 0);
		fail_move = fault;
		CHECK(ffs_listextattr(&ap) == EFAULT && closes == 1);
	}
	reset();
	ap = arguments(NULL, &size, 0);
	access_error = EACCES;
	CHECK(ffs_listextattr(&ap) == EACCES && opens == 0 && closes == 0);
	access_error = 0;
	open_error = EIO;
	CHECK(ffs_listextattr(&ap) == EIO && opens == 1 && closes == 0);
	reset();
	vnode_state.v_type = VCHR;
	CHECK(ffs_listextattr(&ap) == EOPNOTSUPP && opens == 0);
	vnode_state.v_type = VBLK;
	CHECK(ffs_listextattr(&ap) == EOPNOTSUPP && opens == 0);
	reset();
	mount_state.um_fstype = UFS1;
	ap.a_flag = EXTATTR_LIST_LENPREFIX;
#ifdef UFS_EXTATTR
	CHECK(ffs_listextattr(&ap) == EBUSY && opens == 0);
#else
	CHECK(ffs_listextattr(&ap) == EOPNOTSUPP && opens == 0);
#endif
	printf("FFS xattr list contract: PASS (%u checks)\n", checks);
	return 0;
}
