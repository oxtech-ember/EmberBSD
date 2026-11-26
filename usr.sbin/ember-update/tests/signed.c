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
	assert(!eu_channel(channel, "different", issued, &s));
	assert(!eu_channel(channel, "current", issued - 1, &s));
	assert(!eu_channel(channel, "current", expires, &s));
	assert(eu_channel(channel, "current", issued, &s));
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
