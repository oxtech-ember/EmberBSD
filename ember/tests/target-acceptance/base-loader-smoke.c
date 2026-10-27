/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise the target's normal ELF loader on a base shared library. */
#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>

#include <stdio.h>

int
main(void)
{
	void *handle;
	const char *error;

	dlerror();
	handle = dlopen("/lib/libm.so.0", RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		error = dlerror();
		fprintf(stderr, "dlopen base libm: %s\n", error != NULL ? error : "unknown error");
		return (1);
	}

	dlerror();
	if (dlsym(handle, "sin") == NULL) {
		error = dlerror();
		fprintf(stderr, "dlsym base libm sin: %s\n", error != NULL ? error : "unknown error");
		(void)dlclose(handle);
		return (1);
	}
	if (dlclose(handle) != 0) {
		error = dlerror();
		fprintf(stderr, "dlclose base libm: %s\n", error != NULL ? error : "unknown error");
		return (1);
	}

	puts("base ELF loader opened and resolved libm");
	return (0);
}
