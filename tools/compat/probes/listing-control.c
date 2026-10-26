/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
/* Required native primitives and Darwin entry return conventions. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
	char buf[8] = "a";
	char *text, *cursor;
	acl_t acl;
	acl_entry_t entry;
	int result = 1, saved;

	if (strlcat(buf, "bc", sizeof(buf)) != 3 || strcmp(buf, "abc") != 0 ||
	    strcmp(localeconv()->decimal_point, ".") != 0)
		return (1);
	text = strdup("a:b");
	if (text == NULL)
		return (2);
	cursor = text;
	if (strcmp(strsep(&cursor, ":"), "a") != 0 ||
	    strcmp(cursor, "b") != 0) {
		free(text);
		return (3);
	}
	free(text);
	acl = acl_init(0);
	if (acl == NULL)
		return (4);
	if (acl_valid(acl) != 0)
		goto out;
	errno = 0;
	if (acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) != -1 ||
	    errno != EINVAL)
		goto out;
	if (acl_create_entry(&acl, &entry) != 0 ||
	    acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) != 0)
		goto out;
	result = 0;
out:
	saved = errno;
	(void)acl_free(acl);
	errno = saved;
	return (result);
}
