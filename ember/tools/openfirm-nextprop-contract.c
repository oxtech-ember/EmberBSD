/* SPDX-License-Identifier: BSD-2-Clause */
/* Host fixture for the extracted openfirmio OFIOCNEXTPROP case. */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FREAD 1
#define OFIOCNEXTPROP 3
#define FAKE_NODE 7

struct ofiocdesc {
	int of_nodeid;
	int of_namelen;
	char *of_name;
	int of_buflen;
	char *of_buf;
};

static const char * const property_names[] = {
	"first",
	"0123456789ABCDEFGHIJKLMNOPQRSTUV",
	"last"
};
static int tree_available = 1;

static size_t
strlcpy(char *dst, const char *src, size_t size)
{
	size_t len = strlen(src);

	if (size != 0) {
		size_t copy = len < size - 1 ? len : size - 1;
		memcpy(dst, src, copy);
		dst[copy] = '\0';
	}
	return len;
}

static void *
fdtbus_get_data(void)
{
	return tree_available ? (void *)(uintptr_t)1 : NULL;
}

static int
fdtbus_phandle2offset(int phandle)
{
	return phandle == FAKE_NODE ? 0 : -1;
}

static int
fdt_first_property_offset(const void *data, int offset)
{
	(void)data;
	(void)offset;
	return 0;
}

static const void *
fdt_getprop_by_offset(const void *data, int offset, const char **name,
    int *len)
{
	(void)data;
	if (offset < 0 || (size_t)offset >=
	    sizeof(property_names) / sizeof(property_names[0]))
		return NULL;
	*name = property_names[offset];
	*len = (int)strlen(*name);
	return *name;
}

static int
fdt_next_property_offset(const void *data, int offset)
{
	(void)data;
	if (offset + 1 >= (int)(sizeof(property_names) /
	    sizeof(property_names[0])))
		return -1;
	return offset + 1;
}

/* Actual OF_nextprop body, extracted verbatim by the runner. */
/* ACTUAL_OF_NEXTPROP */

static int
openfirmgetstr(int len, char *user, char **cpp)
{
	char *copy;
	if (len < 0 || len > 8191)
		return ENAMETOOLONG;
	copy = malloc((size_t)len + 1);
	if (copy == NULL)
		return ENOMEM;
	memcpy(copy, user, (size_t)len);
	copy[len] = '\0';
	*cpp = copy;
	return 0;
}

static int
copyout(const void *src, void *dst, size_t len)
{
	memcpy(dst, src, len);
	return 0;
}

static int
splhigh(void)
{
	return 0;
}

static void
splx(int state)
{
	(void)state;
}

static int
run_nextprop_case(struct ofiocdesc *of, int flags)
{
	int node = of->of_nodeid, len, ok, error = 0, s = 0;
	char *name = NULL, *value = NULL;
	int cmd = OFIOCNEXTPROP;

	switch (cmd) {
/* ACTUAL_IOCTL_CASE */
	default:
		return ENOTTY;
	}
	if (name != NULL)
		free(name);
	if (value != NULL)
		free(value);
	return error;
}

static int
check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		return 1;
	}
	return 0;
}

static int
test_empty_name(void)
{
	char output[40];
	struct ofiocdesc of = { FAKE_NODE, 0, NULL, 32, output };
	memset(output, 0xa5, sizeof(output));
	if (check(run_nextprop_case(&of, FREAD) == 0,
	    "empty property name succeeds")) return 1;
	if (check(of.of_buflen == 4 && memcmp(output, "name", 4) == 0,
	    "empty property name selects name")) return 1;
	return 0;
}

static int
test_long_name(void)
{
	char output[40], backend[33];
	struct ofiocdesc of = { FAKE_NODE, 5, "first", 32, output };
	memset(output, 0xa5, sizeof(output));
	if (check(run_nextprop_case(&of, FREAD) == 0,
	    "long next property succeeds")) return 1;
	if (check(of.of_buflen == 32 &&
    memcmp(output, property_names[1], 32) == 0 &&
    (unsigned char)output[32] == 0xa5,
	    "ioctl copies exactly the 32-byte property name")) return 1;
	memset(backend, 0xa5, sizeof(backend));
	if (check(OF_nextprop(FAKE_NODE, "first", backend) == 1,
	    "backend returns the next property")) return 1;
	if (check(memcmp(backend, property_names[1], 32) == 0 && backend[32] == '\0',
	    "backend stores 32 bytes plus the terminator")) return 1;
	return 0;
}

static int
test_truncated_destination(void)
{
	char output[5];
	struct ofiocdesc of = { FAKE_NODE, 5, "first", 3, output };
	memset(output, 0xa5, sizeof(output));
	if (check(run_nextprop_case(&of, FREAD) == 0,
	    "truncated destination succeeds")) return 1;
	if (check(of.of_buflen == 3 && memcmp(output, "012", 3) == 0 &&
	    output[3] == (char)0xa5 && output[4] == (char)0xa5,
	    "truncated copy stays inside the caller buffer")) return 1;
	return 0;
}

static int
test_errno_paths(void)
{
	char output[40];
	struct ofiocdesc of = { FAKE_NODE, 4, "last", 32, output };
	memset(output, 0xa5, sizeof(output));
	if (check(run_nextprop_case(&of, FREAD) == ENOENT,
	    "end of property list returns ENOENT")) return 1;
	tree_available = 0;
	of.of_namelen = 0;
	of.of_name = NULL;
	if (check(run_nextprop_case(&of, FREAD) == EINVAL,
	    "invalid FDT state returns EINVAL")) return 1;
	tree_available = 1;
	return 0;
}

int
main(int argc, char **argv)
{
	const char *selected = argc > 1 ? argv[1] : "all";
	int failed = 0;

	if (strcmp(selected, "empty") == 0 || strcmp(selected, "all") == 0)
		failed |= test_empty_name();
	if (strcmp(selected, "long") == 0 || strcmp(selected, "all") == 0)
		failed |= test_long_name();
	if (strcmp(selected, "all") == 0) {
		failed |= test_truncated_destination();
		failed |= test_errno_paths();
	}
	if (strcmp(selected, "empty") != 0 && strcmp(selected, "long") != 0 &&
	    strcmp(selected, "all") != 0) {
		fprintf(stderr, "usage: %s [empty|long|all]\n", argv[0]);
		return 2;
	}
	if (!failed)
		printf("openfirm OFIOCNEXTPROP contract: PASS (%s)\n", selected);
	return failed != 0;
}
