/* Origin: EmberBSD (AI-assisted), production fsck superblock regressions. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/types.h>
#include <sys/param.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "layout.h"

#ifndef NO_FFS_EI
static int endian, needswap, doswap, do_blkswap, do_dirswap;
#define bswap64(v) __builtin_bswap64(v)
#endif
#include "detect.h"

_Static_assert(offsetof(struct fs, fs_sblockloc) == 1000, "disk location ABI");
_Static_assert(offsetof(struct fs, fs_magic) == 1372, "disk magic ABI");

static unsigned int checks;

static void
require(int condition, const char *message)
{

	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		exit(1);
	}
	checks++;
}

static void
make_super(struct fs *fs, uint32_t magic, int64_t location, int swapped,
    int updated)
{

	memset(fs, 0, sizeof(*fs));
	fs->fs_magic = swapped ? __builtin_bswap32(magic) : magic;
	fs->fs_sblockloc = swapped ? __builtin_bswap64(location) : location;
	fs->fs_old_flags = updated ? FS_FLAGS_UPDATED : 0;
}

static void
synthetic_cases(void)
{
	struct fs fs;
	unsigned int i;
	int swapped;
	const uint32_t magic[] = { FS_UFS2_MAGIC, FS_UFS2EA_MAGIC };

	for (swapped = 0; swapped <= 1; swapped++) {
#ifdef NO_FFS_EI
		if (swapped) {
			make_super(&fs, FS_UFS2_MAGIC, SBLOCK_UFS1, 1, 1);
			require(detect_byteorder(&fs, SBLOCK_UFS1) == -1,
			    "native-only build rejects swapped magic");
			continue;
		}
#endif
		for (i = 0; i < sizeof(magic) / sizeof(magic[0]); i++) {
			make_super(&fs, magic[i], SBLOCK_UFS1, swapped, 1);
			require(detect_byteorder(&fs, SBLOCK_UFS2) == -1,
			    "64 KiB alternate must not hide 8 KiB UFS2 primary");
			require(detect_byteorder(&fs, SBLOCK_UFS1) == 0,
			    "8 KiB UFS2 primary remains readable");
			require(detect_byteorder(&fs, -1) == 0,
			    "explicit -b alternate bypasses location check");
			make_super(&fs, magic[i], SBLOCK_UFS2, swapped, 1);
			require(detect_byteorder(&fs, SBLOCK_UFS2) == 0,
			    "standard 64 KiB UFS2 primary remains readable");
			require(detect_byteorder(&fs, SBLOCK_UFS1) == -1,
			    "stale UFS2 copy at 8 KiB is rejected");
		}
		make_super(&fs, FS_UFS1_MAGIC, -123, swapped, 0);
		require(detect_byteorder(&fs, SBLOCK_UFS1) == 0,
		    "old UFS1 has no defined fs_sblockloc");
		require(detect_byteorder(&fs, SBLOCK_FLOPPY) == 0,
		    "old UFS1 floppy placement remains readable");
		require(detect_byteorder(&fs, SBLOCK_UFS2) == -1,
		    "old UFS1 alternate at 64 KiB remains rejected");
		make_super(&fs, FS_UFS1_MAGIC, SBLOCK_UFS1, swapped, 1);
		require(detect_byteorder(&fs, SBLOCK_UFS1) == 0,
		    "updated UFS1 primary remains readable");
		require(detect_byteorder(&fs, SBLOCK_FLOPPY) == -1,
		    "updated UFS1 must match its recorded location");
		require(detect_byteorder(&fs, -1) == 0,
		    "explicit UFS1 alternate remains readable");
#ifndef NO_FFS_EI
		endian = BYTE_ORDER == LITTLE_ENDIAN ? BIG_ENDIAN : LITTLE_ENDIAN;
		make_super(&fs, FS_UFS2_MAGIC, SBLOCK_UFS1, swapped, 1);
		require(detect_byteorder(&fs, SBLOCK_UFS1) == 0,
		    "byte order conversion still accepts the primary");
		require(needswap == 1 && doswap == !swapped &&
		    do_blkswap == !swapped && do_dirswap == !swapped,
		    "byte order conversion flags are preserved");
		endian = 0;
#endif
	}
	make_super(&fs, 0xdeadbeef, SBLOCK_UFS1, 0, 0);
	require(detect_byteorder(&fs, SBLOCK_UFS1) == -1,
	    "unknown magic is rejected");
}

#ifndef NO_FFS_EI
static void
image_case(const char *path)
{
	struct fs primary, alternate;
	FILE *stream;
	int64_t location;
	int32_t blocksize;

	stream = fopen(path, "rb");
	require(stream != NULL, "open generated filesystem");
	require(fseek(stream, SBLOCK_UFS1, SEEK_SET) == 0 &&
	    fread(&primary, sizeof(primary), 1, stream) == 1,
	    "read generated primary");
	require(fseek(stream, SBLOCK_UFS2, SEEK_SET) == 0 &&
	    fread(&alternate, sizeof(alternate), 1, stream) == 1,
	    "read generated alternate");
	require(fclose(stream) == 0, "close generated filesystem");
	require(detect_byteorder(&primary, SBLOCK_UFS1) == 0,
	    "generated primary is accepted");
	location = needswap ? __builtin_bswap64(primary.fs_sblockloc) :
	    primary.fs_sblockloc;
	blocksize = needswap ? __builtin_bswap32(primary.fs_bsize) : primary.fs_bsize;
	require(location == SBLOCK_UFS1 && blocksize == 65536,
	    "fixture has makefs 8 KiB primary and 64 KiB blocks");
	require(memcmp(&primary, &alternate, sizeof(primary)) == 0,
	    "makefs placed a real matching alternate at 64 KiB");
	require(detect_byteorder(&alternate, SBLOCK_UFS2) == -1,
	    "automatic search skips the generated alternate");
	require(detect_byteorder(&primary, SBLOCK_UFS1) == 0,
	    "automatic search reaches the generated primary");
	require(detect_byteorder(&alternate, -1) == 0,
	    "explicit recovery may still use the generated alternate");
}
#endif

int
main(int argc, char **argv)
{

	synthetic_cases();
#ifndef NO_FFS_EI
	if (argc == 2)
		image_case(argv[1]);
#else
	(void)argc;
	(void)argv;
#endif
	printf("PASS: %u superblock selection checks\n", checks);
	return 0;
}
