/* SPDX-License-Identifier: BSD-2-Clause */
/* Run against private fixture trees as an unprivileged user. */
#include <sys/types.h>
#include <stdio.h>
#include "install.h"
int
main(int argc,char **argv)
{
	if (argc!=4)
		return 2;
	merge_defaults(argv[1],argv[2],argv[3]);
	return 0;
}
