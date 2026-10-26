/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
/* Required Darwin ACL iteration and allocation primitives, before probes. */
#include <sys/types.h>
#include <sys/acl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
	acl_t acl;
	acl_entry_t entry;
	void *data, *replacement;
	int result = 1, saved;

	data = malloc(1);
	if (data == NULL)
		return (1);
	replacement = realloc(data, 8);
	if (replacement == NULL) {
		free(data);
		return (1);
	}
	data = replacement;
	memcpy(data, "x\0yz", 4);
	if (memchr(data, '\0', 4) != (char *)data + 1) {
		free(data);
		return (1);
	}
	free(data);
	acl = acl_init(0);
	if (acl == NULL)
		return (1);
	if (acl_valid(acl) == -1)
		goto out;
	errno = 0;
	if (acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) != -1 ||
	    errno != EINVAL)
		goto out;
	if (acl_create_entry(&acl, &entry) == -1 ||
	    acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) != 0)
		goto out;
	result = 0;
out:
	saved = errno;
	(void)acl_free(acl);
	errno = saved;
	return (result);
}
