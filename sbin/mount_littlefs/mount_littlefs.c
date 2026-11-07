/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/param.h>
#include <sys/mount.h>
#include <fs/littlefs/littlefs_mount.h>
#include <err.h>
#include <errno.h>
#include <mntopts.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mountprog.h"

static const struct mntopt options[] = {
	MOPT_STDOPTS,
	MOPT_UPDATE,
	MOPT_GETARGS,
	MOPT_NULL
};

int mount_littlefs(int, char **);

static void __dead
usage(void)
{
	fprintf(stderr, "usage: %s [-b block-size] [-o options] special node\n",
	    getprogname());
	exit(EXIT_FAILURE);
}

int
mount_littlefs(int argc, char **argv)
{
	struct littlefs_args args = { .version = LITTLEFS_ARGSVERSION };
	char device[MAXPATHLEN], node[MAXPATHLEN], *end;
	mntoptparse_t parsed;
	unsigned long block;
	int ch, flags = 0;

	while ((ch = getopt(argc, argv, "b:o:")) != -1) {
		switch (ch) {
		case 'b':
			errno = 0;
			block = strtoul(optarg, &end, 0);
			if (errno || *end || end == optarg || block > UINT32_MAX ||
			    block < DEV_BSIZE || !powerof2(block))
				errx(EXIT_FAILURE, "invalid block size: %s", optarg);
			args.block_size = block;
			break;
		case 'o':
			parsed = getmntopts(optarg, options, &flags, NULL);
			if (parsed == NULL)
				err(EXIT_FAILURE, "getmntopts");
			freemntopts(parsed);
			break;
		default:
			usage();
		}
	}
	if (argc - optind != 2)
		usage();
	pathadj(argv[optind + 1], node);
	if (flags & (MNT_UPDATE | MNT_GETARGS)) {
		args.fspec = argv[optind];
	} else {
		pathadj(argv[optind], device);
		args.fspec = device;
	}
	if (mount(MOUNT_LITTLEFS, node, flags, &args, sizeof(args)) == -1)
		err(EXIT_FAILURE, "%s on %s", argv[optind], node);
	if (flags & MNT_GETARGS)
		printf("block-size=%u\n", args.block_size);
	return EXIT_SUCCESS;
}

#ifndef MOUNT_NOMAIN
int
main(int argc, char **argv)
{
	setprogname(argv[0]);
	return mount_littlefs(argc, argv);
}
#endif
