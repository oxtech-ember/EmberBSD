/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise both file installers on a small test volume. */
#include <string.h>

#include "install.h"

int
main(int argc, char **argv)
{
	enum install_method method;

	if (argc != 4)
		return 2;
	if (strcmp(argv[1], "inactive") == 0)
		method = INSTALL_INACTIVE;
	else if (strcmp(argv[1], "atomic") == 0)
		method = INSTALL_ATOMIC;
	else
		return 2;
	install_file(argv[2], argv[3], 0644, method);
	return 0;
}
