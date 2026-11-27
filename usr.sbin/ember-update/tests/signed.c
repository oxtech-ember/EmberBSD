/* SPDX-License-Identifier: BSD-2-Clause */
/* Run with the published DER envelopes and their independently installed anchor. */
#include <sys/stat.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "catalog.h"
#include "verify.h"

static void *
input(const char *path, size_t *len)
{
	struct stat st;
	assert(stat(path, &st) == 0 && st.st_size > 0 && st.st_size <= EU_MAX_CMS);
	FILE *f = fopen(path, "rb");
	assert(f);
	unsigned char *data = malloc((size_t)st.st_size + 1);
	assert(data);
	*len = fread(data, 1, (size_t)st.st_size, f);
	assert(*len == (size_t)st.st_size && fclose(f) == 0);
	data[*len] = 0;
	return data;
}

static struct eu_json *
member(struct eu_json *object, const char *name)
{
	for (struct eu_json *p = object->child; p != NULL; p = p->next)
		if (eu_equal(p->key, name))
			return p;
	abort();
}

int
main(int argc, char **argv)
{
	if (argc != 4) {
		fprintf(stderr, "usage: signed ROOT.pem current.p7m release.p7m\n");
		return 2;
	}
	size_t cn, rn;
	unsigned char *c = input(argv[2], &cn), *r = input(argv[3], &rn);
	struct eu_json *channel = eu_cms_json(c, cn, argv[1]),
		       *doc = eu_cms_json(r, rn, argv[1]);
	assert(channel && doc);
	time_t issued = (time_t)eu_get(channel, "issued_at")->integer;
	time_t expires = (time_t)eu_get(channel, "expires_at")->integer;
	struct snapshot_ref s;
	assert(eu_equal(eu_channel_error(channel, "different", issued - 1, &s),
	    "invalid signed channel"));
	assert(eu_equal(eu_channel_error(channel, "current", issued - 1, &s),
	    "signed channel is not yet valid. Check the system clock."));
	assert(eu_equal(eu_channel_error(channel, "current", expires, &s),
	    "signed channel has expired. Check the system clock and the channel."));
	assert(eu_channel_error(channel, "current", issued, &s) == NULL);
	assert(eu_channel_error(channel, "current", expires - 1, &s) == NULL);
	char hash[65];
	assert(eu_digest(r, rn, hash) == 0 && eu_equal(hash, s.sha) &&
	       s.size == (int64_t)rn);
	struct release release = {0};
	assert(eu_release(doc, &s, &release));
	assert(release.count > 0);
	free(release.items);
	struct snapshot_ref wrong = s;
	wrong.id = "other";
	assert(!eu_release(doc, &wrong, &release));
	/* Invalid metadata takes precedence over a clock diagnosis. */
	struct eu_json *path = member(member(channel, "release"), "path");
	char first = path->text[0];
	path->text[0] = '/';
	assert(eu_equal(eu_channel_error(channel, "current", issued - 1, &s),
	    "invalid signed channel"));
	path->text[0] = first;
	member(channel, "expires_at")->integer = issued;
	assert(eu_equal(eu_channel_error(channel, "current", issued, &s),
	    "invalid signed channel"));
	assert(!eu_cms_json(c, cn + 1, argv[1]) && !eu_cms_json(r, rn - 1, argv[1]));
	c[cn - 1] ^= 1;
	r[rn - 1] ^= 1;
	assert(!eu_cms_json(c, cn, argv[1]) && !eu_cms_json(r, rn, argv[1]));
	eu_json_free(doc);
	eu_json_free(channel);
	free(c);
	free(r);
	return 0;
}
