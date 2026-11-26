/* SPDX-License-Identifier: BSD-2-Clause */
/* Populate only the inactive FFS root. The controller owns the signed plan. */
#include <sys/param.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sha2.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "install.h"
#include "profile.h"

#define DB "var/db/ember-update"
#define PLAN "emberbsd.install-plan.v1"
#define LOCAL "emberbsd.local-kernel.v1"
#define SELF "/usr/libexec/ember-install-slot"
#define ROOTDB "/usr/pkg/pkgdb"

struct input {
	const struct eu_json *plan, *os, *packages;
	const char *dir, *commit;
	int local, comp;
	char digest[65];
};

static void
run(const char *program, ...)
{
	va_list ap;
	const char *argv[32];
	size_t n = 0;
	int status;
	pid_t pid;

	argv[n++] = program;
	va_start(ap, program);
	do {
		if (n == __arraycount(argv))
			errx(1, "too many command arguments");
		argv[n] = va_arg(ap, const char *);
	} while (argv[n++] != NULL);
	va_end(ap);
	pid = fork();
	if (pid < 0)
		err(1, "fork");
	if (pid == 0) {
		execv(program, __UNCONST(argv));
		warn("%s", program);
		_exit(1);
	}
	while (waitpid(pid, &status, 0) == -1)
		if (errno != EINTR)
			err(1, "waitpid");
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		errx(1, "%s failed", program);
}

const char *
install_relpath(const char *v)
{
	const char *p = v;
	size_t n;
	if (!*v)
		errx(1, "empty path");
	do {
		n = strcspn(p, "/");
		if (!n || (n == 1 && *p == '.') || (n == 2 && !strncmp(p, "..", 2)) ||
		    n != strspn(p,
			     "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.+"))
			errx(1, "unsafe profile or artifact path: %s", v);
		p += n;
	} while (*p++ == '/');
	return v;
}

const char *
install_component(const struct eu_json *v)
{
	const char *s = eu_string(v);
	if (!s || !*s || *s == '-' || strchr(s, '/'))
		errx(1, "expected a file name");
	return install_relpath(s);
}

void
install_mkdirs(const char *root, const char *rel)
{
	char path[MAXPATHLEN], *p;
	struct stat st;
	slot_path(path, sizeof(path), root, install_relpath(rel));
	for (p = path + strlen(root) + (strcmp(root, "/") != 0);; p++) {
		int last = !*p;
		if (!last && *p != '/')
			continue;
		*p = 0;
		if ((mkdir(path, 0755) && errno != EEXIST) || lstat(path, &st) ||
		    !S_ISDIR(st.st_mode))
			errx(1, "invalid directory: %s", path);
		if (last)
			break;
		*p = '/';
	}
}

static void
hash_file(const char *dir, const char *leaf, char *sha)
{
	char path[MAXPATHLEN];
	struct stat st;
	slot_path(path, sizeof(path), dir, leaf);
	if (lstat(path, &st) || !S_ISREG(st.st_mode) || !SHA256_File(path, sha))
		errx(1, "cannot hash regular file %s", path);
}

static const struct eu_json *
read_json(const char *dir, const char *leaf)
{
	char path[MAXPATHLEN];
	struct eu_json *j;
	slot_path(path, sizeof(path), dir, leaf);
	if ((j = eu_json_file(path, 1024 * 1024)) == NULL)
		err(1, "%s", path);
	return j;
}

const struct eu_json *
install_array(const struct eu_json *j, const char *name)
{
	const struct eu_json *a = eu_get(j, name);
	if (!a || a->type != EU_ARRAY)
		errx(1, "expected array: %s", name);
	return a->child;
}

static int
has(const struct eu_json *array, const char *item)
{
	for (; array; array = array->next)
		if (!strcmp(install_component(array), item))
			return 1;
	return 0;
}

static void
check_target(const struct eu_json *a, const struct eu_json *b)
{
	const char *keys[] = {"abi", "arch", "runtime", "triplet"};
	for (size_t i = 0; i < __arraycount(keys); i++)
		if (strcmp(field(a, keys[i]), field(b, keys[i])))
			errx(1, "target %s mismatch", keys[i]);
}

/* A root-owned child below /var/tmp is protected by the sticky parent.
 * All entries below the selected directory must be private to its owner. */
void
install_trusted_input(const char *directory, const char *leaf)
{
	char path[MAXPATHLEN], *p;
	struct stat st;
	int last;
	if (directory[0] != '/' || strstr(directory, "/../") || strstr(directory, "/./"))
		errx(1, "invalid staging path");
	slot_path(path, sizeof(path), directory, leaf);
	for (p = path + 1;; p++) {
		last = !*p;
		if (!last && *p != '/')
			continue;
		*p = 0;
		if (lstat(path, &st) || st.st_uid ||
		    (last ? !S_ISREG(st.st_mode) : !S_ISDIR(st.st_mode)) ||
		    ((st.st_mode & 022) &&
			(last || !(st.st_mode & S_ISVTX) || strlen(path) >= strlen(directory))))
			errx(1, "untrusted staging path: %s", path);
		if (last)
			return;
		*p = '/';
	}
}

static void
load_input(struct input *in, const struct slot *s, const char *kind, const char *dir)
{
	const struct eu_json *descriptor, *generation, *p, *sets, *want;
	struct stat st;
	char path[MAXPATHLEN], sha[65];
	int count = 0;
	memset(in, 0, sizeof(*in));
	in->dir = dir;
	in->local = !strcmp(kind, "prepare-kernel");
	if (!in->local && strcmp(kind, "prepare"))
		errx(1, "invalid installation mode");
	if (dir[0] != '/' || lstat(dir, &st) || !S_ISDIR(st.st_mode) || st.st_uid ||
	    (st.st_mode & 022))
		errx(1, "staging requires a root-owned directory");
	if (in->local) {
		install_trusted_input(dir, "netbsd");
		for (p = install_array(s->profile, "modules"); p; p = p->next) {
			char member[MAXPATHLEN];
			if (snprintf(member, sizeof(member), "modules/%s.kmod", install_component(p)) >=
			    (int)sizeof(member))
				errx(1, "module path too long");
			install_trusted_input(dir, member);
		}
		check_loader_inputs(s, dir);
		hash_file(dir, "netbsd", sha);
		/* The same canonical bytes become the trial identity in the new root. */
		char receipt[160];
		int len = snprintf(receipt, sizeof(receipt),
		    "{\"schema\":\"" LOCAL "\",\"sha256\":\"%s\"}\n", sha);
		SHA256_Data((const uint8_t *)receipt, len, in->digest);
		return;
	}
	install_trusted_input(dir, "plan.json");
	in->plan = read_json(dir, "plan.json");
	if (strcmp(field(in->plan, "schema"), PLAN) ||
	    strcmp(field(in->plan, "profile"), field(s->profile, "profile")))
		errx(1, "incompatible installation plan");
	check_target(eu_get(in->plan, "target"), eu_get(s->profile, "target"));
	in->packages = install_array(in->plan, "packages");
	hash_file(dir, "plan.json", in->digest);
	descriptor = eu_get(in->plan, "os_update_sha256");
	if (!descriptor)
		errx(1, "missing OS selection");
	if (descriptor->type != EU_NULL) {
		install_trusted_input(dir, "update.json");
		in->os = read_json(dir, "update.json");
		hash_file(dir, "update.json", sha);
		if (!slot_hash(eu_string(descriptor)) || strcmp(sha, descriptor->text) ||
		    strcmp(field(in->os, "schema"), "emberbsd.os-update.v1") ||
		    strcmp(field(in->os, "profile"), field(s->profile, "profile")))
			errx(1, "OS descriptor does not match plan");
		check_target(eu_get(in->os, "target"), eu_get(s->profile, "target"));
		in->commit = field(in->os, "os_commit");
		if (strlen(in->commit) != 40 || strspn(in->commit, "0123456789abcdef") != 40 ||
		    strcmp(in->commit, field(in->plan, "os_commit")))
			errx(1, "invalid OS build identity");
		generation = eu_get(in->os, "generation");
		p = eu_get(in->plan, "os_generation");
		if (!generation || generation->type != EU_INTEGER || generation->integer < 1 ||
		    !p || p->type != EU_INTEGER || generation->integer != p->integer)
			errx(1, "OS generation mismatch");
		sets = eu_get(in->os, "sets");
		want = install_array(s->profile, "sets");
		if (!sets || sets->type != EU_OBJECT)
			errx(1, "missing distribution sets");
		for (p = sets->child; p; p = p->next) {
			if (!has(want, p->key) ||
			    (strcmp(p->key, "base") && strcmp(p->key, "etc") &&
				strcmp(p->key, "boot") && strcmp(p->key, "comp")))
				errx(1, "unexpected set: %s", p->key);
			count++;
		}
		for (p = want; p; p = p->next) {
			char setname[NAME_MAX + 1];
			snprintf(setname, sizeof(setname), "%s.tar.xz", install_component(p));
			install_trusted_input(dir, setname);
			if (!eu_get(sets, install_component(p)))
				errx(1, "missing distribution set");
			count--;
		}
		in->comp = has(want, "comp");
		if (count || !has(want, "etc") || !has(want, "base") || !has(want, "boot"))
			errx(1, "unsupported distribution set selection");
	} else if (!in->packages)
		errx(1, "empty update");
	for (p = in->packages; p; p = p->next) {
		const char *name = install_component(eu_get(p, "name"));
		const struct eu_json *manual = eu_get(p, "manual");
		if (manual && (manual->type != EU_BOOL || !manual->integer))
			errx(1, "invalid manual package selection");
		const struct eu_json *size = eu_get(p, "size");
		char leaf[NAME_MAX + 10];
		if (strlen(name) > NAME_MAX - 4 || !size || size->type != EU_INTEGER ||
		    size->integer <= 0 || !slot_hash(field(p, "sha256")))
			errx(1, "invalid package plan record");
		snprintf(leaf, sizeof(leaf), "packages/%s.tgz", name);
		install_trusted_input(dir, leaf);
		slot_path(path, sizeof(path), dir, leaf);
		hash_file(dir, leaf, sha);
		if (stat(path, &st) || st.st_size != size->integer ||
		    strcmp(sha, field(p, "sha256")))
			errx(1, "package differs from verified plan: %s", name);
	}
}

void
install_copy(const char *srcdir, const char *src, const char *dstdir, const char *dst,
    mode_t mode, enum install_method method)
{
	char a[MAXPATHLEN], b[MAXPATHLEN];
	check_parents(srcdir, src);
	check_parents(dstdir, dst);
	slot_path(a, sizeof(a), srcdir, src);
	slot_path(b, sizeof(b), dstdir, dst);
	install_file(a, b, mode, method);
}

/* Compare class, byte order and machine with the running base userland. */
static bool
native_elf(const unsigned char *kernel)
{
	unsigned char header[20];
	int fd = open("/bin/sh", O_RDONLY);
	if (fd == -1 || read(fd, header, sizeof(header)) != sizeof(header) || close(fd))
		errx(1, "cannot identify the running ELF ABI");
	return !memcmp(header, "\177ELF", 4) && !memcmp(kernel, header, 6) &&
	    !memcmp(kernel + 18, header + 18, 2);
}

/* Check both kernel file formats and the build ID carried by the booted kernel. */
void
install_check_kernel(const char *dir, const char *name, const char *commit, const char *tag, int raw)
{
	char path[MAXPATHLEN], id[52] = {0};
	struct stat st;
	unsigned char *map;
	int fd, size;

	slot_path(path, sizeof(path), dir, name);
	fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 64 ||
	    (uintmax_t)st.st_size > SIZE_MAX ||
	    (map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED)
		err(1, "%s", path);
	if ((raw && memcmp(map + 56, "ARMd", 4)) || (!raw && !native_elf(map)))
		errx(1, "unexpected kernel format: %s", path);
	if (commit) {
		size = snprintf(id, sizeof(id), "EmberBSD:%s", commit);
		if (memmem(map, st.st_size, tag, strlen(tag)) == NULL ||
		    (memmem(map, st.st_size, id, size + 1) == NULL &&
			(id[size] = '\n', memmem(map, st.st_size, id, size + 2) == NULL)))
			errx(1, "unexpected kernel or OS build: %s", path);
	}
	munmap(map, st.st_size);
	close(fd);
}

static void
update_sets(const struct input *in, const char *root, const char *work)
{
	char base[MAXPATHLEN], etc[MAXPATHLEN], comp[MAXPATHLEN], old[MAXPATHLEN], new[MAXPATHLEN],
	    baseline[MAXPATHLEN];
	slot_path(base, sizeof(base), in->dir, "base.tar.xz");
	slot_path(etc, sizeof(etc), in->dir, "etc.tar.xz");
	slot_path(comp, sizeof(comp), in->dir, "comp.tar.xz");
	slot_path(old, sizeof(old), work, "old");
	slot_path(new, sizeof(new), work, "new");
	slot_path(baseline, sizeof(baseline), root, DB "/etc.tar.xz");
	if (mkdir(old, 0700) || mkdir(new, 0700))
		err(1, "create defaults directories");
	/* A set is installed over a snapshot. Retire only the original service
	 * listed as obsolete by this signed successor. */
	char service[MAXPATHLEN];
	check_parents(root, "usr/libexec/ember-os-update");
	slot_path(service,sizeof(service),root,"usr/libexec/ember-os-update");
	if (access(service,F_OK)==0)
		run("/usr/libexec/ember-stage-ffs","obsolete-base",root,base,work,NULL);
	run("/bin/tar", "-xpf", base, "-C", root, "--safe-writes", "--exclude", "./etc/*",
	    "--exclude", "./var/*", "--exclude", "./usr/pkg", "--exclude", "./usr/local",
	    "--exclude", "./libdata/firmware", "--exclude", "./usr/libdata/firmware", NULL);
	run("/bin/tar", "-xpf", base, "-C", root, "--safe-writes", "./etc/release",
	    "./etc/mtree/set.base", NULL);
	if (in->comp)
		run("/bin/tar", "-xpf", comp, "-C", root, "--safe-writes", NULL);
	run("/bin/tar", "-xpf", baseline, "-C", old, NULL);
	run("/bin/tar", "-xpf", etc, "-C", new, NULL);
	merge_defaults(root, old, new);
	install_file(etc, baseline, 0600, INSTALL_ATOMIC);
	install_copy(in->dir, "update.json", root, DB "/os-release.json", 0600, INSTALL_ATOMIC);
}

/* Install the shared ELF kernel and modules before the adapter prepares its boot files. */
static void
boot_files(
    const struct slot *s, const struct input *in, const char *root, int dest, const char *work)
{
	const struct eu_json *p, *mods = install_array(s->profile, "modules");
	const char *moddir = install_relpath(field(s->profile, "module_directory"));
	const char *src = in->dir;
	char tmp[MAXPATHLEN], bundle[MAXPATHLEN], path[MAXPATHLEN], a[MAXPATHLEN], b[MAXPATHLEN];
	const char *tag = field(s->profile, "kernel");
	FILE *f;
	char *line = NULL;
	size_t cap = 0;

	if (in->os) {
		slot_path(tmp, sizeof(tmp), work, "boot");
		if (mkdir(tmp, 0700))
			err(1, "%s", tmp);
		slot_path(bundle, sizeof(bundle), in->dir, "boot.tar.xz");
		run("/bin/tar", "-xpf", bundle, "-C", tmp, NULL);
		src = tmp;
	}
	if (in->os || in->local) {
		install_check_kernel(src, "netbsd", in->commit, tag, 0);
		install_copy(src, "netbsd", root, "netbsd", 0555, INSTALL_ATOMIC);
		slot_path(path, sizeof(path), root, "etc/modules.conf");
		f = fopen(path, "r");
		if (!f && errno != ENOENT)
			err(1, "%s", path);
		while (f && getline(&line, &cap, f) >= 0) {
			char *first;
			line[strcspn(line, "#")] = 0;
			first = strtok(line, " \t\r\n");
			if (first && !has(mods, first))
				errx(1, "release has no configured module: %s", first);
		}
		if (f && (ferror(f) || fclose(f)))
			err(1, "%s", path);
		free(line);
		check_parents(root, moddir);
		slot_path(path, sizeof(path), root, moddir);
		run("/bin/rm", "-rf", path, NULL);
		for (p = mods; p; p = p->next) {
			const char *m = install_component(p);
			if (strlen(m) > NAME_MAX - 6)
				errx(1, "module name too long");
			slot_path(path, sizeof(path), moddir, m);
			install_mkdirs(root, path);
			snprintf(a, sizeof(a), "modules/%s.kmod", m);
			snprintf(b, sizeof(b), "%s.kmod", m);
			char dst[MAXPATHLEN];
			slot_path(dst, sizeof(dst), path, b);
			install_copy(src, a, root, dst, 0444, INSTALL_ATOMIC);
		}
	}
	install_loader_files(s, dest, in->os || in->local ? src : NULL, in->commit);
}

static int
automatic(const char *root, const char *name)
{
	char path[MAXPATHLEN], entry[MAXPATHLEN], *line = NULL;
	size_t cap = 0;
	DIR *dir;
	struct dirent *e;
	const char *end = strrchr(name, '-'), *ver;
	FILE *info;
	int found = 0, flag = 1;
	if (!end || end == name)
		errx(1, "invalid full package name");
	slot_path(path, sizeof(path), root, "usr/pkg/pkgdb");
	dir = opendir(path);
	if (!dir)
		err(1, "%s", path);
	while ((e = readdir(dir))) {
		ver = strrchr(e->d_name, '-');
		if (!ver || ver - e->d_name != end - name || strncmp(name, e->d_name, end - name))
			continue;
		slot_path(entry, sizeof(entry), path, e->d_name);
		char contents[MAXPATHLEN];
		slot_path(contents, sizeof(contents), entry, "+CONTENTS");
		if (access(contents, F_OK) != 0)
			continue;
		if (found++)
			errx(1, "ambiguous installed package: %s", name);
		flag = 0;
		slot_path(contents, sizeof(contents), entry, "+INSTALLED_INFO");
		info = fopen(contents, "r");
		if (!info && errno != ENOENT)
			err(1, "%s", contents);
		while (info && getline(&line, &cap, info) >= 0) {
			line[strcspn(line, "\r\n")] = 0;
			if (!strcasecmp(line, "automatic=YES"))
				flag = 1;
		}
		if (info && (ferror(info) || fclose(info)))
			err(1, "%s", contents);
	}
	closedir(dir);
	free(line);
	return flag;
}

/* Remove private package copies on both success and pkg_add failure. */
static char package_dir[MAXPATHLEN];

static void
clean_packages(void)
{
	if (!package_dir[0])
		return;
	DIR *dir = opendir(package_dir);
	if (dir != NULL) {
		struct dirent *e;
		while ((e = readdir(dir)) != NULL)
			if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..") &&
			    unlinkat(dirfd(dir), e->d_name, 0) == -1)
				warn("remove %s/%s", package_dir, e->d_name);
		closedir(dir);
	}
	if (rmdir(package_dir))
		warn("remove %s", package_dir);
	package_dir[0] = 0;
}

static void
packages(const struct input *in, const char *root)
{
	char pkgs[MAXPATHLEN], tmp[MAXPATHLEN], config[MAXPATHLEN], src[MAXPATHLEN],
	    dst[MAXPATHLEN];
	const struct eu_json *p;
	char inside[MAXPATHLEN], db[40] = ROOTDB;
	FILE *f;
	if (!in->packages)
		return;
	check_parents(root, "var/tmp/incoming");
	slot_path(tmp, sizeof(tmp), root, "var/tmp/ember-pkgs.XXXXXXXX");
	if (atexit(clean_packages) || !mkdtemp(tmp))
		err(1, "make package stage");
	strlcpy(package_dir, tmp, sizeof(package_dir));
	slot_path(config, sizeof(config), tmp, "pkg_install.conf");
	if (!(f = fopen(config, "wx")) ||
	    fputs("VERIFIED_INSTALLATION=always\nCERTIFICATE_ANCHOR_PKGS=/etc/openssl/"
		  "emberbsd-release-root.pem\n",
		f) == EOF ||
	    fclose(f))
		err(1, "%s", config);
	strlcpy(inside, !strcmp(root, "/") ? tmp : tmp + strlen(root), sizeof(inside));
	if (setenv("PKG_PATH", inside, 1) || setenv("PKG_DBDIR", db, 1))
		err(1, "package environment");
	install_mkdirs(root, "usr/pkg/pkgdb");
	slot_path(pkgs, sizeof(pkgs), in->dir, "packages");
	for (p = in->packages; p; p = p->next) {
		char leaf[NAME_MAX + 1], sha[65];
		const char *name = install_component(eu_get(p, "name"));
		snprintf(leaf, sizeof(leaf), "%s.tgz", name);
		slot_path(src, sizeof(src), pkgs, leaf);
		slot_path(dst, sizeof(dst), tmp, leaf);
		install_file(src, dst, 0600, INSTALL_ATOMIC);
		if (!SHA256_File(dst, sha) || strcmp(sha, field(p, "sha256")))
			errx(1, "staged package hash mismatch: %s", name);
	}
	for (p = in->packages; p; p = p->next) {
		const char *name = field(p, "name");
		char leaf[NAME_MAX + 1], conf[MAXPATHLEN];
		bool manual = eu_get(p, "manual") != NULL;
		int dependency = !manual && automatic(root, name);
		snprintf(leaf, sizeof(leaf), "%s.tgz", name);
		slot_path(conf, sizeof(conf), inside, "pkg_install.conf");
		slot_path(src, sizeof(src), root, "usr/pkg/pkgdb");
		slot_path(dst, sizeof(dst), src, name);
		slot_path(src, sizeof(src), dst, "+CONTENTS");
		if (access(src, R_OK) || !manual) {
			slot_path(dst, sizeof(dst), inside, leaf);
			run("/usr/sbin/chroot", root, "/usr/sbin/pkg_add",
			    dependency ? "-Au" : "-u", "-K", db, "-C", conf, dst, NULL);
		}
		if (manual)
			run("/usr/sbin/chroot", root, "/usr/sbin/pkg_admin", "-K", db, "unset",
			    "automatic", name, NULL);
		if (access(src, R_OK))
			errx(1, "package was not registered: %s", name);
	}
	clean_packages();
}

static void
fstab(const struct slot *s, const char *root, int dest)
{
	char path[MAXPATHLEN], temp[MAXPATHLEN], *line = NULL, *p, *start;
	const char *old = s->label[s->running];
	size_t cap = 0;
	int fd, rows = 0;
	FILE *in, *out;
	struct stat st;
	check_parents(root, "etc/fstab");
	slot_path(path, sizeof(path), root, "etc/fstab");
	slot_path(temp, sizeof(temp), root, "etc/.fstab.XXXXXXXX");
	if (lstat(path, &st) || !S_ISREG(st.st_mode) || !(in = fopen(path, "r")) ||
	    (fd = mkstemp(temp)) < 0 || !(out = fdopen(fd, "w")))
		err(1, "fstab");
	while (getline(&line, &cap, in) >= 0) {
		p = line + strspn(line, " \t");
		start = p;
		if (!strncmp(p, "NAME=", 5) && !strncmp(p + 5, old, strlen(old)) &&
		    strchr(" \t", p[5 + strlen(old)]) != NULL && p[5 + strlen(old)] != 0) {
			p += 5 + strlen(old);
			const char *mount = p + strspn(p, " \t");
			if (*mount == '/' && strspn(mount + 1, " \t") > 0) {
				const char *type = mount + 1 + strspn(mount + 1, " \t");
				if (!strncmp(type, "ffs", 3) && strspn(type + 3, " \t") > 0) {
					if (fprintf(out, "%.*sNAME=%s%s", (int)(start - line), line,
						s->label[dest], p) < 0)
						err(1, "fstab");
					rows++;
					continue;
				}
			}
		}
		if (fputs(line, out) == EOF)
			err(1, "fstab");
	}
	if (rows != 1 || ferror(in) || fclose(in) || fflush(out) ||
	    fchown(fd, st.st_uid, st.st_gid) || fchmod(fd, st.st_mode & 07777) || fsync(fd) ||
	    fclose(out) || rename(temp, path))
		errx(1, "cannot install one named FFS root in fstab");
	free(line);
}

static void
populate(const struct slot *s, int dest, const struct input *in, const char *root)
{
	char work[MAXPATHLEN], path[MAXPATHLEN], sha[65];
	struct statvfs mount;
	if (statvfs(root, &mount) || strcmp(mount.f_mntonname, root) ||
	    strcmp(mount.f_mntfromname, s->device[dest]) || strcmp(mount.f_fstypename, "ffs") ||
	    dest == s->running)
		errx(1, "incorrect mounted destination");
	if (s->good != s->running || s->back != s->running || s->next >= 0 || s->prepared >= 0)
		errx(1, "the inactive slot must be disarmed before installation");
	strlcpy(work, root, sizeof(work));
	char *slash = strrchr(work, '/');
	if (!slash || strcmp(slash, "/root"))
		errx(1, "expected private work directory");
	*slash = 0;
	install_mkdirs(root, DB);
	if (in->os)
		update_sets(in, root, work);
	boot_files(s, in, root, dest, work);
	fstab(s, root, dest);
	packages(in, root);
	if (in->local) {
		hash_file(root, "netbsd", sha);
		slot_path(path, sizeof(path), root, DB "/local-kernel.json");
		int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
		if (fd < 0 ||
		    dprintf(fd, "{\"schema\":\"" LOCAL "\",\"sha256\":\"%s\"}\n", sha) < 0 ||
		    fsync(fd) || close(fd))
			err(1, "%s", path);
		install_copy(root, DB "/local-kernel.json", root, DB "/installed.json", 0600, INSTALL_ATOMIC);
	} else
		install_copy(in->dir, "plan.json", root, DB "/installed.json", 0600, INSTALL_ATOMIC);
	hash_file(root, DB "/installed.json", sha);
	if (strcmp(sha, in->digest))
		errx(1, "installed plan readback failed");
}

int
main(int argc, char **argv)
{
	struct slot s;
	struct input in;
	char work[] = "/var/run/ember-slot.XXXXXXXX";
	char dir[MAXPATHLEN];
	int dest, pos;
	if (argc == 3 && !strcmp(argv[1], "packages")) {
		if (geteuid() != 0)
			errx(1, "package installation requires root");
		slot_lock();
		s.profile = slot_profile();
		if (eu_profile_adapter(s.profile) != EU_ADAPTER_SINGLE)
			errx(1, "live package update requires a single-root profile");
		load_input(&in, &s, "prepare", argv[2]);
		const struct eu_json *gen = eu_get(in.plan, "os_generation"),
				     *commit = eu_get(in.plan, "os_commit");
		if (in.os || !gen || gen->type != EU_INTEGER || gen->integer || !commit ||
		    commit->type != EU_NULL)
			errx(1, "single-root installation accepts only packages");
		packages(&in, "/");
		return 0;
	}
	bool mounted = argc == 6 && !strcmp(argv[1], "populate");
	if (geteuid() != 0 || (!mounted && argc != 4))
		errx(1, "usage: ember-install-slot packages DIRECTORY | prepare|prepare-kernel A|B DIRECTORY");
	pos = mounted ? 3 : 1;
	dest = slot_id(argv[pos + 1]);
	if (!realpath(argv[pos + 2], dir))
		err(1, "%s", argv[pos + 2]);
	if (!mounted)
		slot_lock();
	slot_layout(&s);
	load_input(&in, &s, argv[pos], dir);
	if (mounted) {
		populate(&s, dest, &in, argv[2]);
		return 0;
	}
	slot_inactive(&s, dest);
	if (!mkdtemp(work))
		err(1, "create slot work directory");
	slot_begin_prepare(&s, dest);
	run("/usr/libexec/ember-stage-ffs", work, s.device[dest], argv[pos], argv[pos + 1], dir,
	    NULL);
	slot_receipt(&s, dest, in.digest);
	return 0;
}
