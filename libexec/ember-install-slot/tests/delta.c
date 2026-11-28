/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise only the tree comparison on two test directories. */
#include "install.h"

int
main(int argc, char *argv[])
{
	if (argc != 3)
		return 2;
	install_delta(argv[1], argv[2]);
	return 0;
}
