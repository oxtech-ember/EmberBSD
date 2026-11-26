/* SPDX-License-Identifier: BSD-2-Clause */
/* Read the volatile boot receipt from the EFI runtime service. */

#include <sys/types.h>
#include <sys/efiio.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

int efi_trial(const char *);
int efi_reset_capable(const char *);

static int
read_receipt(const char *key, char *data, size_t size)
{
	uint16_t name[32];
	struct efi_var_ioc v = {
		.name = name,
		.vendor = { 0x404b8673, 0x6bc3, 0x4602, 0xaa, 0x7d,
		    { 0x4d, 0xbb, 0x89, 0x75, 0xe2, 0xb5 } },
		.data = data,
		.datasize = size
	};
	int fd, e, rv;

	v.namesize = (strlen(key) + 1) * sizeof(name[0]);
	if (v.namesize > sizeof(name)) {
		errno = EINVAL;
		return -1;
	}
	for (size_t i = 0; i < v.namesize / sizeof(name[0]); i++)
		name[i] = (unsigned char)key[i];
	if ((fd = open("/dev/efi", O_RDONLY | O_CLOEXEC)) == -1)
		return -1;
	rv = ioctl(fd, EFIIOC_VAR_GET, &v);
	e = errno;
	(void)close(fd);
	if (rv == -1) {
		errno = e;
		return -1;
	}
	if (v.attrib != (EFI_VARIABLE_BOOTSERVICE_ACCESS |
	    EFI_VARIABLE_RUNTIME_ACCESS) || v.datasize != size) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int
efi_trial(const char *running)
{
	char data[2];

	if (running == NULL || (running[0] != 'A' && running[0] != 'B') ||
	    running[1] != '\0') {
		errno = EINVAL;
		return -1;
	}
	if (read_receipt("EmberBootSlot", data, sizeof(data)) == -1)
		return -1;
	if (data[0] != running[0] || (data[1] != 'C' && data[1] != 'T')) {
		errno = EINVAL;
		return -1;
	}
	return data[1] == 'T';
}

int
efi_reset_capable(const char *provider)
{
	char data[32];
	size_t size;

	if (provider == NULL || (size = strlen(provider)) == 0 ||
	    size > sizeof(data))
		return 0;
	return read_receipt("EmberBootReset", data, size) == 0 &&
	    memcmp(data, provider, size) == 0;
}
