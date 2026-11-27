/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <fetch.h>
#include <openssl/evp.h>
#include "delivery.h"

volatile sig_atomic_t stopping;
#include "verify.h"

static int
valid_origin(const char *s)
{
	const char *p;
	if (s == NULL || strncmp(s, "https://", 8) != 0 || !s[8])
		return 0;
	for (p = s + 8; *p; p++)
		if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ012345"
			    "6789.:-",
			    *p))
			return 0;
	return 1;
}

/* Retain metadata in memory. Stream archives to a private new file. */
static int
receive(const char *url, const char *path, uint64_t limit, const char *expected,
	unsigned char **output, size_t *length)
{
	fetchIO *stream = NULL;
	FILE *file = NULL;
	EVP_MD_CTX *md = NULL;
	unsigned char block[32768], digest[EVP_MAX_MD_SIZE], *data = NULL;
	char hex[65];
	size_t capacity = 0;
	uint64_t total = 0;
	ssize_t n;
	unsigned len, i;
	int fd = -1, saved, ok = -1;
	if (path != NULL) {
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
			  0600);
		if (fd == -1)
			return -1;
		file = fdopen(fd, "wb");
		if (!file)
			goto done;
	}
	if ((md = EVP_MD_CTX_new()) == NULL ||
	    EVP_DigestInit_ex(md, EVP_sha256(), NULL) != 1)
		goto done;
	fetchTimeout = 60;
	/* Do not accept the unverified libfetch environment setting in a service. */
	if (unsetenv("SSL_NO_VERIFY_PEER") < 0)
		goto done;
	if (!(stream = fetchGetURL(url, "Ad"))) {
		errno = EIO;
		goto done;
	}
	while ((n = fetchIO_read(stream, block, sizeof(block))) > 0) {
		if (stopping) {
			errno = EINTR;
			goto done;
		}
		if ((uint64_t)n > limit - total) {
			errno = EFBIG;
			goto done;
		}
		if (EVP_DigestUpdate(md, block, (size_t)n) != 1)
			goto done;
		if (file) {
			if (fwrite(block, 1, (size_t)n, file) != (size_t)n)
				goto done;
		} else {
			size_t needed = (size_t)total + (size_t)n;
			if (needed > capacity) {
				size_t next = capacity ? capacity * 2 : 32768;
				if (next < needed)
					next = needed;
				if (next > limit)
					next = (size_t)limit;
				unsigned char *grown = realloc(data, next);
				if (!grown)
					goto done;
				data = grown;
				capacity = next;
			}
			memcpy(data + total, block, (size_t)n);
		}
		total += (uint64_t)n;
	}
	if (n < 0 || EVP_DigestFinal_ex(md, digest, &len) != 1 || len != 32) {
		errno = EIO;
		goto done;
	}
	for (i = 0; i < 32; i++)
		snprintf(hex + 2 * i, 3, "%02x", digest[i]);
	if (expected && (total != limit || strcmp(expected, hex) != 0)) {
		errno = EINVAL;
		goto done;
	}
	if (file && (fflush(file) < 0 || fsync(fd) < 0))
		goto done;
	if (output) {
		*output = data;
		data = NULL;
		*length = (size_t)total;
	}
	ok = 0;
done:
	saved = errno;
	if (stream)
		fetchIO_close(stream);
	if (file) {
		if (fclose(file) < 0 && !ok) {
			saved = errno;
			ok = -1;
		}
	} else if (fd >= 0)
		close(fd);
	if (ok && path)
		unlink(path);
	EVP_MD_CTX_free(md);
	free(data);
	errno = saved;
	return ok;
}

const char *
eu_snapshot(struct delivery *d, const char *base, const char *channel,
	    const char *anchor)
{
	char url[8192];
	unsigned char *buf = NULL;
	size_t n;
	const char *error = "cannot verify signed channel and release";
	const char *channel_error;
	memset(d, 0, sizeof(*d));
	if (!valid_origin(base) || !eu_slug(channel) || strlen(channel) > 127 ||
	    strlen(base) > 1024)
		return "invalid release origin or channel name";
	d->base = strdup(base);
	d->channel = strdup(channel);
	if (!d->base || !d->channel)
		goto done;
	snprintf(url, sizeof(url), "%s/%s.p7m", base, channel);
	if (receive(url, NULL, EU_MAX_CMS, NULL, &buf, &n) < 0 ||
	    !(d->pointer = eu_cms_json(buf, n, anchor)))
		goto done;
	channel_error = eu_channel_error(d->pointer, channel, time(NULL), &d->ref);
	if (channel_error != NULL) {
		error = channel_error;
		goto done;
	}
	free(buf);
	buf = NULL;
	snprintf(url, sizeof(url), "%s/%s", base, d->ref.path);
	if (receive(url, NULL, (uint64_t)d->ref.size, d->ref.sha, &buf, &n) < 0 ||
	    !(d->manifest = eu_cms_json(buf, n, anchor)) ||
	    !eu_release(d->manifest, &d->ref, &d->release))
		goto done;
	d->metadata = strdup(d->ref.path);
	if (!d->metadata)
		goto done;
	*strrchr(d->metadata, '/') = 0;
	error = NULL;
done:
	free(buf);
	if (error != NULL)
		eu_snapshot_free(d);
	return error;
}

void
eu_snapshot_free(struct delivery *d)
{
	free(d->base);
	free(d->channel);
	free(d->metadata);
	free(d->release.items);
	eu_json_free(d->pointer);
	eu_json_free(d->manifest);
	memset(d, 0, sizeof(*d));
}

int
eu_payload(const struct delivery *d, const struct artifact *a, const char *path,
	   unsigned char **data, size_t *n)
{
	char url[8192];
	int count;
	if (a->size < 0 || (!path && a->size > EU_MAX_CONTENT)) {
		errno = EFBIG;
		return -1;
	}
	if (eu_equal(a->kind, "package-index"))
		count = snprintf(url, sizeof(url), "%s/%s/%s", d->base, d->metadata,
				 a->path);
	else {
		const char *arch = eu_string(eu_get(a->target, "arch"));
		const char *name = strrchr(a->path, '/');
		if (!eu_slug(arch)) {
			errno = EINVAL;
			return -1;
		}
		count = snprintf(url, sizeof(url), "%s/%s/target/%s/%s", d->base,
				 d->channel, arch, name ? name + 1 : a->path);
	}
	if (count < 0 || (size_t)count >= sizeof(url)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return receive(url, path, (uint64_t)a->size, a->sha, data, n);
}
