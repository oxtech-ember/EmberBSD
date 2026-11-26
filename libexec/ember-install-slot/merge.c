/* SPDX-License-Identifier: BSD-2-Clause */
/* Merge two pristine distribution trees with a snapshot of the site's files. */
#include <sys/param.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <sha2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "install.h"

struct identity {
	mode_t mode;
	uid_t uid;
	gid_t gid;
	char content[MAXPATHLEN];
	ino_t ino;
	nlink_t links;
};

/* Do not follow site configuration links while reading or replacing a child. */
void
check_parents(const char *root, const char *leaf)
{
	char path[MAXPATHLEN], *p;
	struct stat st;
	slot_path(path, sizeof(path), root, leaf);
	for (p = path + strlen(root) + (strcmp(root, "/") != 0); (p = strchr(p, '/')) != NULL; p++) {
		*p = 0;
		if (lstat(path, &st) == 0) {
			if (!S_ISDIR(st.st_mode))
				errx(1, "unexpected path component: %s", path);
		} else if (errno != ENOENT)
			err(1, "%s", path);
		*p = '/';
	}
}

static void
identify(const char *path, struct identity *id)
{
	ssize_t n;
	struct stat st;
	memset(id, 0, sizeof(*id));
	if (lstat(path, &st) == -1) {
		if (errno == ENOENT)
			return;
		err(1, "%s", path);
	}
	id->mode = st.st_mode;
	id->uid = st.st_uid;
	id->gid = st.st_gid;
	id->ino = st.st_ino;
	id->links = st.st_nlink;
	if (S_ISREG(st.st_mode)) {
		if (!SHA256_File(path, id->content))
			err(1, "%s", path);
	} else if (S_ISLNK(st.st_mode)) {
		n = readlink(path, id->content, sizeof(id->content));
		if (n < 0 || (size_t)n >= sizeof(id->content))
			err(1, "readlink %s", path);
		id->content[n] = 0;
	} else if (!S_ISDIR(st.st_mode))
		errx(1, "unsupported configuration entry: %s", path);
}

static int
equal(const struct identity *a, const struct identity *b)
{
	return a->mode == b->mode && a->uid == b->uid && a->gid == b->gid &&
	       !strcmp(a->content, b->content);
}

void
install_file(const char *src, const char *dst, mode_t mode, enum install_method method)
{
	char tmp[MAXPATHLEN], buf[32768];
	int in, out, n;
	ssize_t count, pos, w;

	in = open(src, O_RDONLY | O_NOFOLLOW);
	if (in == -1)
		err(1, "open %s", src);
	if (method == INSTALL_INACTIVE) {
		/* Preparation disarms the inactive slot before replacing its files. */
		if (unlink(dst) == -1 && errno != ENOENT)
			err(1, "remove %s", dst);
		out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	} else {
		n = snprintf(tmp, sizeof(tmp), "%s.XXXXXXXX", dst);
		if (n < 0 || (size_t)n >= sizeof(tmp))
			errx(1, "path too long");
		out = mkstemp(tmp);
	}
	if (out == -1)
		err(1, "create %s", dst);
	while ((count = read(in, buf, sizeof(buf))) > 0)
		for (pos = 0; pos < count; pos += w) {
			w = write(out, buf + pos, count - pos);
			if (w <= 0) {
				if (w == 0)
					errno = EIO;
				goto failed;
			}
		}
	if (count < 0 || fchmod(out, mode) || fsync(out) || close(out) ||
	    (method == INSTALL_ATOMIC && rename(tmp, dst)))
		goto failed;
	close(in);
	return;

failed:
	n = errno;
	(void)close(out);
	(void)unlink(method == INSTALL_ATOMIC ? tmp : dst);
	errno = n;
	err(1, "install %s", dst);
}

struct link_group {
	ino_t ino, old_ino;
	int changed;
	char *first;
	struct link_group *next;
};

static struct link_group *
group(struct link_group **list, ino_t ino)
{
	struct link_group *g;
	for (g = *list; g; g = g->next)
		if (g->ino == ino)
			return g;
	g = calloc(1, sizeof(*g));
	if (!g)
		err(1, "allocate link group");
	g->ino = ino;
	g->next = *list;
	return *list = g;
}

static struct link_group *
changed_links(const char *old, const char *new)
{
	struct link_group *list = NULL, *g;
	char *paths[] = {__UNCONST(new), NULL};
	FTS *ft = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
	FTSENT *e;
	char path[MAXPATHLEN];
	struct identity a, b;
	if (!ft)
		err(1, "open defaults tree");
	for (;;) {
		errno = 0;
		e = fts_read(ft);
		if (!e)
			break;
		if (e->fts_info == FTS_ERR || e->fts_info == FTS_NS || e->fts_info == FTS_DNR)
			errx(1, "invalid defaults: %s", e->fts_path);
		if (e->fts_info != FTS_F || e->fts_statp->st_nlink < 2)
			continue;
		const char *leaf = e->fts_path + strlen(new) + 1;
		check_parents(old, leaf);
		slot_path(path, sizeof(path), old, leaf);
		identify(path, &a);
		identify(e->fts_path, &b);
		g = group(&list, b.ino);
		if (!equal(&a, &b) || a.links != b.links || (g->old_ino && a.ino != g->old_ino))
			g->changed = 1;
		g->old_ino = a.ino;
	}
	if (errno || fts_close(ft))
		err(1, "scan hard links");
	return list;
}

static void
apply(const char *dst, const char *src, struct identity *new, struct link_group **links)
{
	struct link_group *g;
	struct stat st;
	if (new->mode == 0) {
		if (lstat(dst, &st))
			err(1, "%s", dst);
		if (S_ISDIR(st.st_mode)) {
			if (rmdir(dst) && errno != ENOTEMPTY && errno != EEXIST)
				err(1, "%s", dst);
		} else if (unlink(dst))
			err(1, "%s", dst);
		return;
	}
	if (S_ISDIR(new->mode)) {
		if (mkdir(dst, 0700) && errno != EEXIST)
			err(1, "%s", dst);
	} else if (S_ISLNK(new->mode)) {
		if ((unlink(dst) && errno != ENOENT) || symlink(new->content, dst) ||
		    lchown(dst, new->uid, new->gid))
			err(1, "%s", dst);
		return;
	} else {
		if (new->links > 1) {
			g = group(links, new->ino);
			if (g->first) {
				if ((unlink(dst) && errno != ENOENT) || link(g->first, dst))
					err(1, "%s", dst);
				return;
			}
			if (!(g->first = strdup(dst)))
				err(1, "allocate hard link");
		}
		install_file(src, dst, new->mode & 07777, INSTALL_ATOMIC);
	}
	if (chown(dst, new->uid, new->gid) || chmod(dst, new->mode & 07777))
		err(1, "%s", dst);
}

static void
walk(const char *root, const char *old, const char *new, int remove)
{
	const char *base = remove ? old : new, *leaf;
	char *paths[] = {__UNCONST(base), NULL};
	char a[MAXPATHLEN], b[MAXPATHLEN], site[MAXPATHLEN];
	struct identity before, after, local;
	struct link_group *links = remove ? NULL : changed_links(old, new), *g;
	int relink;
	FTS *tree;
	FTSENT *e;
	mode_t diff;

	if ((tree = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL)) == NULL)
		err(1, "fts_open");
	for (;;) {
		errno = 0;
		e = fts_read(tree);
		if (!e)
			break;
		if (e->fts_info == FTS_ERR || e->fts_info == FTS_DNR || e->fts_info == FTS_NS)
			errx(1, "read configuration: %s", e->fts_path);
		if (e->fts_level == 0 || (remove ? e->fts_info == FTS_D : e->fts_info == FTS_DP))
			continue;
		leaf = e->fts_path + strlen(base) + 1;
		check_parents(new, leaf);
		check_parents(old, leaf);
		slot_path(a, sizeof(a), old, leaf);
		slot_path(b, sizeof(b), new, leaf);
		slot_path(site, sizeof(site), root, leaf);
		identify(b, &after);
		if (remove && after.mode)
			continue;
		identify(a, &before);
		relink = S_ISREG(after.mode) &&
			 (after.links > 1 ? group(&links, after.ino)->changed : before.links > 1);
		if (!relink && equal(&before, &after))
			continue;
		check_parents(root, leaf);
		identify(site, &local);
		if (!relink && equal(&after, &local))
			continue;
		if (before.mode && after.mode && ((before.mode ^ after.mode) & S_IFMT))
			errx(1, "configuration type changed: %s", leaf);
		if (!equal(&before, &local) && !(relink && equal(&local, &after))) {
			diff = before.mode ^ local.mode;
			if (!S_ISREG(before.mode) || !S_ISREG(after.mode) || before.links != 1 ||
			    after.links != 1 || !diff || (diff & ~0044) || (local.mode & diff) ||
			    before.mode != after.mode || before.uid != local.uid ||
			    before.gid != local.gid || after.uid != local.uid ||
			    after.gid != local.gid ||
			    (strcmp(local.content, before.content) &&
				strcmp(local.content, after.content)))
				errx(1, "local and release changes overlap: %s", leaf);
			after.mode = local.mode;
		}
		apply(site, b, &after, &links);
	}
	if (errno != 0 || fts_close(tree))
		err(1, "read configuration tree");
	while ((g = links) != NULL) {
		links = g->next;
		free(g->first);
		free(g);
	}
}

void
merge_defaults(const char *root, const char *old, const char *new)
{
	walk(root, old, new, 1);
	walk(root, old, new, 0);
}
