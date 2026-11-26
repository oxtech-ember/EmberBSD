/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/disk.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "slot.h"
#include "profile.h"

#define PROFILE "/etc/ember-update-profile.json"

void
slot_path(char *dst, size_t cap, const char *dir, const char *leaf)
{
	int n = snprintf(dst, cap, "%s%s%s", dir, !strcmp(dir, "/") ? "" : "/", leaf);
	if (n < 0 || (size_t)n >= cap)
		errx(1, "path too long: %s", leaf);
}

const char *
field(const struct eu_json *obj, const char *key)
{
	const char *v = eu_string(eu_get(obj, key));
	if (v == NULL || *v == '\0')
		errx(1, "invalid field: %s", key);
	return v;
}

void
slot_lock(void)
{
	int fd =
	    open("/var/run/ember-slot-io.lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || (st.st_mode & 077))
		errx(1, "unsafe slot lock");
	if (flock(fd, LOCK_EX | LOCK_NB))
		err(1, "slot operation in progress");
}

int
slot_id(const char *s)
{
	if (s != NULL && s[0] != '\0' && s[1] == '\0') {
		if (*s == 'A')
			return 0;
		if (*s == 'B')
			return 1;
	}
	errx(1, "expected slot A or B");
}

int
slot_hash(const char *s)
{
	return s != NULL && strlen(s) == 64 && strspn(s, "0123456789abcdef") == 64;
}

/* One more byte than the largest valid record distinguishes truncated reads. */
static ssize_t
record(const struct slot *s, const char *leaf, char *buf, size_t cap)
{
	char path[MAXPATHLEN];
	int fd;
	ssize_t n;

	slot_path(path, sizeof(path), s->markers, leaf);
	fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd == -1 && errno == ENOENT)
		return 0;
	if (fd == -1 || (n = read(fd, buf, cap)) < 0)
		err(1, "%s", path);
	if (close(fd) != 0)
		err(1, "%s", path);
	return n;
}

static int
marker(const struct slot *s, const char *leaf)
{
	char b[2];
	return record(s, leaf, b, sizeof(b)) == 1 && (*b == 'A' || *b == 'B') ? *b - 'A' : -1;
}

static void
write_record(const struct slot *s, const char *leaf, const char *value, int fixed)
{
	char path[MAXPATHLEN], check[70];
	struct stat st;
	size_t n = strlen(value);
	int fd, dir;

	slot_path(path, sizeof(path), s->markers, leaf);
	fd = open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC | (fixed ? 0 : O_CREAT), 0644);
	if (fd == -1 || fstat(fd, &st) == -1)
		err(1, "%s", path);
	if (!S_ISREG(st.st_mode) || (fixed && st.st_size != 1))
		errx(1, "invalid loader record: %s", path);
	if (pwrite(fd, value, n, 0) != (ssize_t)n || (!fixed && ftruncate(fd, n) == -1) ||
	    fsync(fd) == -1 || close(fd) == -1)
		err(1, "%s", path);
	dir = open(s->markers, O_RDONLY | O_DIRECTORY);
	if (dir == -1 || fsync(dir) == -1 || close(dir) == -1)
		err(1, "%s", s->markers);
	if (record(s, leaf, check, sizeof(check)) != (ssize_t)n || memcmp(check, value, n))
		errx(1, "loader record readback failed: %s", path);
}

void
slot_marker(const struct slot *s, const char *leaf, int index)
{
	char value[] = "-";
	if (index >= 0)
		*value = 'A' + index;
	write_record(s, leaf, value, 1);
}

void
slot_receipt(const struct slot *s, int index, const char *sha)
{
	char buf[69] = "-\n";
	if (sha != NULL) {
		if (!slot_hash(sha))
			errx(1, "invalid preparation digest");
		snprintf(buf, sizeof(buf), "%c %s\n", 'A' + index, sha);
	}
	write_record(s, "osplan.dat", buf, 0);
}

static void
wedge(const char *path, struct dkwedge_info *w)
{
	char raw[MAXPATHLEN];
	int fd;

	if (getdiskrawname(raw, sizeof(raw), path) == NULL)
		err(1, "raw wedge for %s", path);
	fd = open(raw, O_RDONLY | O_CLOEXEC);
	if (fd == -1 || ioctl(fd, DIOCGWEDGEINFO, w) == -1)
		err(1, "get wedge %s", raw);
	close(fd);
}

static void
devices(struct slot *s)
{
	struct dkwedge_info root, esp, all[128], *w;
	struct dkwedge_list wl = {.dkwl_buf = all, .dkwl_bufsize = sizeof(all)};
	struct statvfs rm, em, *mounts;
	struct stat disk, source;
	char path[MAXPATHLEN], raw[MAXPATHLEN];
	int fd, n, i, j, found = 0;
	unsigned k;

	if (statvfs("/", &rm) || statvfs("/boot", &em))
		err(1, "mount table");
	if (strcmp(rm.f_fstypename, "ffs") || strcmp(em.f_fstypename, "msdos") ||
	    strcmp(em.f_mntonname, "/boot"))
		errx(1, "expected FFS root and FAT ESP at /boot");
	wedge(rm.f_mntfromname, &root);
	wedge(em.f_mntfromname, &esp);
	if (strcmp((char *)esp.dkw_wname, s->esp) || strcmp(root.dkw_parent, esp.dkw_parent))
		errx(1, "ESP and root must belong to the configured disk");
	fd = opendisk(root.dkw_parent, O_RDONLY, raw, sizeof(raw), 0);
	if (fd == -1 || ioctl(fd, DIOCLWEDGES, &wl) == -1)
		err(1, "list root disk wedges");
	close(fd);
	if (wl.dkwl_ncopied != wl.dkwl_nwedges)
		errx(1, "incomplete wedge list");
	s->running = -1;
	for (k = 0; k < wl.dkwl_ncopied; k++) {
		w = &all[k];
		if (strcmp((char *)w->dkw_wname, s->esp) == 0)
			found++;
		for (j = 0; j < 2; j++) {
			if (strcmp((char *)w->dkw_wname, s->label[j]))
				continue;
			if (s->device[j][0] || strcmp(w->dkw_ptype, DKW_PTYPE_FFS))
				errx(1, "ambiguous or non-FFS slot %c", 'A' + j);
			slot_path(s->device[j], sizeof(s->device[j]), "/dev", w->dkw_devname);
			if (!strcmp(w->dkw_devname, root.dkw_devname))
				s->running = j;
		}
	}
	if (found != 1 || s->running < 0 || !s->device[0][0] || !s->device[1][0])
		errx(1, "incomplete A/B disk layout");
	n = getmntinfo(&mounts, MNT_NOWAIT);
	if (n <= 0)
		err(1, "mount table");
	for (j = 0; j < 2; j++) {
		strlcpy(path, s->device[j], sizeof(path));
		if (stat(path, &disk) || !S_ISBLK(disk.st_mode))
			errx(1, "invalid block device: %s", path);
		for (i = 0; i < n; i++) {
			if (!strcmp(path, mounts[i].f_mntfromname) ||
			    (stat(mounts[i].f_mntfromname, &source) == 0 &&
				S_ISBLK(source.st_mode) && source.st_rdev == disk.st_rdev))
				s->mounted[j] = 1;
		}
	}
}

/* Read the FDT chosen property emitted only for a hardware watchdog trial. */
static int
uboot_trial(void)
{
	char line[512];
	FILE *stream = popen("/usr/sbin/ofctl -p /chosen", "r");
	int trial = 0;
	if (stream == NULL)
		err(1, "ofctl");
	while (fgets(line, sizeof(line), stream) != NULL)
		if (strcspn(line, " \t\r\n") == strlen("emberbsd,trial-watchdog") &&
		    strncmp(line, "emberbsd,trial-watchdog", strlen("emberbsd,trial-watchdog")) ==
			0)
			trial = 1;
	if (pclose(stream) != 0)
		errx(1, "cannot read /chosen");
	return trial;
}

static void
check_machine(const char *key, const char *expected)
{
	char value[256];
	size_t n = sizeof(value);
	if (sysctlbyname(key, value, &n, NULL, 0) || !n || value[n - 1] != 0 ||
	    strcmp(value, expected))
		errx(1, "hardware profile mismatch: %s", key);
}

struct eu_json *
slot_profile(void)
{
	struct stat st;
	struct eu_json *profile;
	if (lstat(PROFILE, &st) || !S_ISREG(st.st_mode) || st.st_uid || (st.st_mode & 022))
		errx(1, "unsafe or missing " PROFILE);
	if ((profile = eu_json_file(PROFILE, 64 * 1024)) == NULL)
		err(1, PROFILE);
	return profile;
}

void
slot_layout(struct slot *s)
{
	char buf[69], running[2];
	const struct eu_json *roots;
	enum eu_adapter adapter;
	ssize_t n;

	memset(s, 0, sizeof(*s));
	s->profile = slot_profile();
	adapter = eu_profile_adapter(s->profile);
	if (adapter != EU_ADAPTER_UBOOT && adapter != EU_ADAPTER_EFI)
		errx(1, "unsupported slot adapter or storage");
	s->efi = adapter == EU_ADAPTER_EFI;
	check_machine("hw.model", field(s->profile, "model"));
	const struct eu_json *dmi = eu_get(s->profile, "dmi");
	if (dmi) {
		check_machine("machdep.dmi.system-product", field(dmi, "product"));
		check_machine("machdep.dmi.system-vendor", field(dmi, "vendor"));
	}
	roots = eu_get(s->profile, "roots");
	s->label[0] = field(roots, "A");
	s->label[1] = field(roots, "B");
	s->esp = field(s->profile, "esp");
	if (!strcmp(s->label[0], s->label[1]) || !strcmp(s->label[0], s->esp) ||
	    !strcmp(s->label[1], s->esp))
		errx(1, "duplicate profile labels");
	devices(s);
	s->markers = s->efi ? "/boot/EFI/EmberBSD" : "/boot";
	running[0] = 'A' + s->running;
	running[1] = 0;
	s->trial = s->efi ? efi_trial(running) : uboot_trial();
	if (s->trial < 0)
		errx(1, "loader does not identify this boot");
	s->good = marker(s, "osgood.dat");
	s->back = marker(s, "osback.dat");
	s->next = marker(s, "osnext.dat");
	s->prepared = -1;
	n = record(s, "osplan.dat", buf, sizeof(buf));
	if (n == 67 && (buf[0] == 'A' || buf[0] == 'B') && buf[1] == ' ' && buf[66] == '\n') {
		buf[66] = 0;
		if (slot_hash(buf + 2)) {
			s->prepared = buf[0] - 'A';
			strlcpy(s->digest, buf + 2, sizeof(s->digest));
		}
	}
}

void
slot_inactive(struct slot *s, int dest)
{
	int confirmed = s->good >= 0 ? s->good : s->back;
	if (s->efi && !efi_reset_capable(field(s->profile, "reset_provider")))
		errx(1, "firmware has no matching watchdog handoff for trial boots");
	if (s->running != confirmed || s->running == dest || s->mounted[dest] || s->next >= 0 ||
	    (s->trial && s->good != s->running))
		errx(1, "operation needs a confirmed root and an unmounted inactive slot");
}

void
slot_begin_prepare(const struct slot *s, int dest)
{
	if (s->good != s->running)
		slot_marker(s, "osgood.dat", s->running);
	if (s->back != s->running)
		slot_marker(s, "osback.dat", s->running);
	slot_receipt(s, dest, NULL);
}
