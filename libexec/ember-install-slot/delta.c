/* SPDX-License-Identifier: BSD-2-Clause */
/* Compare a read-only snapshot with a privately mounted, disarmed FFS. */
#include <sys/param.h>
#include <sys/extattr.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fts.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "install.h"

struct entry {
	char *name;
	bool keep;
};

static int
by_name(const void *av, const void *bv)
{
	const struct entry *a = av, *b = bv;
	return strcmp(a->name, b->name);
}

static void
join(char *buf, const char *root, const char *rel)
{
	if (snprintf(buf, MAXPATHLEN, "%s/%s", root, rel) >= MAXPATHLEN)
		errx(3, "snapshot path too long");
}

static bool
no_attributes(const char *path)
{
	const int spaces[] = {EXTATTR_NAMESPACE_USER, EXTATTR_NAMESPACE_SYSTEM};
	for (size_t i = 0; i < __arraycount(spaces); i++) {
		ssize_t n = extattr_list_link(path, spaces[i], NULL, 0);
		if (n < 0 && errno == EOPNOTSUPP)
			continue;
		if (n < 0)
			err(3, "read extended attributes: %s", path);
		if (n > 0)
			return false;
	}
	return true;
}

static bool
same_regular(const char *src, const struct stat *a, const char *dst, const struct stat *b)
{
	if (!S_ISREG(a->st_mode) || a->st_mode != b->st_mode ||
	    a->st_nlink != 1 || b->st_nlink != 1 || a->st_uid != b->st_uid ||
	    a->st_gid != b->st_gid || a->st_flags != b->st_flags ||
	    a->st_size != b->st_size || a->st_mtim.tv_sec != b->st_mtim.tv_sec ||
	    a->st_mtim.tv_nsec != b->st_mtim.tv_nsec ||
	    a->st_blocks != b->st_blocks ||
	    a->st_blocks < a->st_size / DEV_BSIZE + (a->st_size % DEV_BSIZE != 0) ||
	    !no_attributes(src) || !no_attributes(dst))
		return false;
	int equal = install_files_equal(src, dst);
	if (equal < 0)
		err(3, "compare %s", dst);
	return equal == 1;
}

/* The NUL list comes from find -x on the snapshot. The full copy uses it too. */
void
install_delta(const char *snapshot, char *target)
{
	struct entry *entries = NULL;
	size_t count = 0, used = 0, cap = 0;
	char *line = NULL, rel[MAXPATHLEN], src[MAXPATHLEN];
	uintmax_t reused = 0, bytes = 0, copied = 0;
	ssize_t len;
	struct stat st, top;

	while ((len = getdelim(&line, &cap, '\0', stdin)) >= 0) {
		if (!len || line[len - 1] != '\0' ||
		    (strcmp(line, ".") && strncmp(line, "./", 2)) ||
		    strstr(line, "/../") || strstr(line, "/./") || strstr(line, "//") ||
		    !strcmp(strrchr(line, '/') ? strrchr(line, '/') + 1 : "", ".."))
			errx(1, "invalid snapshot file list");
		if (count == used) {
			used = used ? used * 2 : 1024;
			if (reallocarr(&entries, used, sizeof(*entries)))
				errx(3, "cannot allocate snapshot file list");
		}
		entries[count] = (struct entry){.name = strdup(line)};
		if (!entries[count].name)
			err(3, "allocate snapshot path");
		count++;
	}
	free(line);
	if (!feof(stdin) || ferror(stdin) || !count)
		errx(3, "cannot read snapshot file list");
	qsort(entries, count, sizeof(*entries), by_name);
	if (strcmp(entries[0].name, ".") || lstat(target, &top) || !S_ISDIR(top.st_mode))
		errx(1, "incorrect delta root");

	char *paths[] = {target, NULL};
	FTS *tree = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR | FTS_XDEV, NULL);
	if (!tree)
		err(3, "open inactive tree");
	FTSENT *p;
	for (;;) {
		errno = 0;
		if (!(p = fts_read(tree))) {
			if (errno)
				err(3, "scan inactive tree");
			break;
		}
		if (p->fts_info == FTS_ERR || p->fts_info == FTS_NS ||
		    p->fts_info == FTS_DNR || p->fts_info == FTS_DC)
			errx(3, "cannot traverse %s", p->fts_path);
		if (p->fts_statp->st_dev != top.st_dev)
			errx(1, "unexpected mount in inactive root: %s", p->fts_path);
		if (p->fts_info == FTS_DP) {
			if (p->fts_number && rmdir(p->fts_accpath))
				err(3, "remove obsolete directory %s", p->fts_path);
			continue;
		}
		if (snprintf(rel, sizeof(rel), ".%s", p->fts_path + strlen(target)) >=
		    (int)sizeof(rel))
			errx(3, "inactive path too long");
		struct entry key = {.name = rel};
		struct entry *want = bsearch(&key, entries, count, sizeof(*entries), by_name);
		if (want) {
			join(src, snapshot, rel);
			if (lstat(src, &st))
				err(3, "read snapshot %s", rel);
		}
		if (p->fts_info == FTS_D) {
			p->fts_number = !want || !S_ISDIR(st.st_mode);
			if (!p->fts_level && p->fts_number)
				errx(1, "snapshot root must be a directory");
			if (!p->fts_number &&
			    (p->fts_statp->st_flags || !no_attributes(p->fts_accpath)))
				errx(3, "use a fresh directory for %s", p->fts_path);
		} else if (want && same_regular(src, &st, p->fts_accpath, p->fts_statp)) {
			want->keep = true;
			reused++;
			bytes += st.st_size;
		} else if (unlink(p->fts_accpath)) {
			err(3, "remove obsolete entry %s", p->fts_path);
		}
	}
	if (fts_close(tree))
		err(3, "close inactive tree");
	for (size_t i = 0; i < count; i++) {
		if (!entries[i].keep) {
			join(src, snapshot, entries[i].name);
			if (lstat(src, &st))
				err(3, "read snapshot %s", src);
			copied += S_ISREG(st.st_mode);
			size_t n = strlen(entries[i].name) + 1;
			if (fwrite(entries[i].name, 1, n, stdout) != n)
				err(3, "write changed paths");
		}
		free(entries[i].name);
	}
	free(entries);
	if (fflush(stdout))
		err(3, "write changed paths");
	openlog("ember-install-slot", LOG_PERROR, LOG_DAEMON);
	syslog(LOG_NOTICE, "FFS package stage: reused %" PRIuMAX " regular files "
	    "(%" PRIuMAX " bytes); %" PRIuMAX " regular paths to copy", reused, bytes, copied);
	closelog();
}
