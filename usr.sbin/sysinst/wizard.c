/* Origin: EmberBSD sysinst text wizard, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysctl.h>
#include <sys/disk.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>
#include <curses.h>
#include <fcntl.h>
#include <pwd.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sha2.h>
#include "defs.h"
#include "msg_defs.h"
#include "menu_defs.h"
#include "wizard_ui.h"
#include "wizard.h"
#include "wizard_console.h"

bool wizard_active, wizard_automatic, wizard_installing;
bool wizard_custom;
bool wizard_initial_back, wizard_expert_requested;
static bool custom_palette;
static int stage;
static bool net_source;
static char source_url[STRSIZE];
static char hostname[64] = "emberbsd";
static char username[32] = "";
static char wizard_timezone[64] = "UTC";
static const char *profile = "Standard (without X11)";
static int profile_index;
static bool profile_ready;
static int desktop_index;
static char desktop_bundle[STRSIZE], desktop_sum[129];
static char desktop_cache[STRSIZE];
static bool desktop_mounted;
static const char *activity = "Preparing installation";
static char source_error[128];
static char boot_medium[STRSIZE];
static bool desktop_clear(void);

static bool
screen_ready(void)
{
	struct winsize size;
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0 &&
	    size.ws_row > 0 && size.ws_col > 0 &&
	    (size.ws_row != getmaxy(stdscr) || size.ws_col != getmaxx(stdscr)))
	{
		if (resizeterm(size.ws_row, size.ws_col) == ERR)
			return false;
		if (mainwin != NULL)
			wresize(mainwin, size.ws_row - 2, size.ws_col - 2);
	}
	return getmaxy(stdscr) >= WIZARD_ROWS && getmaxx(stdscr) >= WIZARD_COLS;
}

static void
draw(const struct wizard_frame *f)
{
	static const short rgb_foreground[] = { 7, 2, 1, 3, 6, 4, 7, 5, 0 };
	static const short fallback_foreground[] = {
		COLOR_WHITE, COLOR_WHITE, COLOR_RED, COLOR_YELLOW, COLOR_YELLOW,
		COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_BLACK
	};
	int top, left;
	erase();
	/* Legacy helper windows and font changes can invalidate cached cells. */
	clearok(stdscr, TRUE);
	if (!screen_ready()) {
		addnstr("Resize terminal to 80 columns and 25 rows. Esc: back.",
		    getmaxx(stdscr) - 1);
		refresh();
		return;
	}
	wizard_frame_origin(getmaxy(stdscr), getmaxx(stdscr), &top, &left);
	if (has_colors()) {
		for (int p = 0; p <= WIZARD_SELECTED; p++)
			init_pair(3 + p, custom_palette ? rgb_foreground[p] : fallback_foreground[p],
			    p == WIZARD_SELECTED ? COLOR_YELLOW : COLOR_BLACK);
	}
	for (int y = 0; y < WIZARD_ROWS; y++) {
		for (int x = 0; x < WIZARD_COLS - 1; x++) {
			int p = f->color[y][x];
			attr_t attr = has_colors() ? COLOR_PAIR(3 + p) :
			    (p == WIZARD_SELECTED ? A_REVERSE : A_NORMAL);
			if (p == WIZARD_STRONG || (!custom_palette &&
			    (p == WIZARD_YELLOW || p == WIZARD_RED || p == WIZARD_AMBER)))
				attr |= A_BOLD;
			attrset(attr);
			mvaddch(top + y, left + x, f->text[y][x]);
		}
	}
	attrset(A_NORMAL);
	keypad(stdscr, TRUE);
	curs_set(0);
	touchwin(stdscr);
	refresh();
}

static void
frame(struct wizard_frame *f, const char *title)
{
	char label[64];
	wizard_frame_init(f, stage, title);
	snprintf(label, sizeof(label), "EMBERBSD %s", EMBER_VERSION);
	wizard_frame_text(f, 0, 3, label, WIZARD_AMBER);
	if (wizard_installing) {
		wizard_frame_text(f, 2, 44, "                                 ", WIZARD_MUTED);
		wizard_frame_text(f, 2, 44, "Changes are now being written.", WIZARD_MUTED);
	}
}

static int
choose(const char *title, const char * const *options, int count, int first)
{
	struct wizard_frame f;
	int key, pos = first, focus = 0;
	bool settings_form = stage == 4 && count == 5 &&
	    strcmp(options[4], "Continue to review") == 0;
	bool source_form = stage == 2 && count == 5 &&
	    strcmp(title, "Choose your installation source") == 0;
	bool system_form = stage == 3 && count == 4;
	bool desktop_form = stage == 3 &&
	    strcmp(title, "Choose your graphics environment") == 0;

	for (;;) {
		int spacing = stage == 3 ? 3 : (stage == 4 ? 2 : 1);
		int visible = 1 + 9 / spacing;
		if (system_form) {
			spacing = 2;
			visible = 4;
		}
		if (stage == 1) {
			spacing = 2;
			visible = 2;
		}
		int start = pos >= visible ? pos - visible + 1 : 0;
		frame(&f, title);
		static const char * const instructions[] = {
			"Choose how you want to use the installer.",
			"Choose a target disk. Check its model and size.",
			"Use this USB image or download a matching build.",
			"Install a ready set or choose the components.",
			"A few settings for the first start.",
			"Check the target and the changes below."
		};
		if (stage >= 0 && stage < 6)
			wizard_frame_text(&f, 6, 22, instructions[stage], WIZARD_MUTED);
		for (int i = start; i < count && i < start + visible; i++) {
			if (settings_form) {
				if (i < 4) {
					char line[54];
					snprintf(line, sizeof(line), "%-53.53s", options[i]);
					wizard_frame_text(&f, 9 + 2 * i, 22, line,
					    i == pos ? WIZARD_SELECTED : WIZARD_TEXT);
				}
			} else if (source_form) {
				if (i < 2)
					wizard_frame_choice(&f, 9 + 4 * i, options[i], i == pos);
			} else if (system_form && i == 3)
				wizard_frame_text(&f, 17, 22, options[i],
				    i == pos ? WIZARD_SELECTED : WIZARD_AMBER);
			else
				wizard_frame_choice(&f, 9 + spacing * (i - start), options[i], i == pos);
			if (system_form && i < 3) {
				static const char * const detail[] = {
					"Base and developer tools; no X11.",
					"The essential system with fewer tools.",
					"Choose individual binary sets."
				};
				wizard_frame_text(&f, 10 + 2 * i, 26, i == 0 && desktop_index ?
				    "Base and developer tools; X11 with desktop." : detail[i], WIZARD_MUTED);
			}
		}
		if (system_form)
			wizard_frame_text(&f, 19, 22, desktop_index == 2 ?
			    "KDE Plasma experimental profile. SSH is enabled." : desktop_index ?
			    "Wi-Fi manager with XFCE. SSH is enabled." :
			    "Wi-Fi via console. SSH is enabled.", WIZARD_MUTED);
		if (desktop_form) {
			wizard_frame_text(&f, 6, 22,
			    "Optional. Installed together with the system.", WIZARD_MUTED);
			wizard_frame_text(&f, 10, 26, "Console system without desktop packages.", WIZARD_MUTED);
			wizard_frame_text(&f, 13, 26, "X11 desktop and Wi-Fi manager.", WIZARD_MUTED);
			wizard_frame_text(&f, 16, 26, "Experimental Wayland desktop; matching bundle required.", WIZARD_MUTED);
			wizard_frame_text(&f, 19, 22, "Desktop packages are verified before disk writes.", WIZARD_MUTED);
		}
		if (settings_form)
			wizard_frame_text(&f, 17, 22,
			    "Root password    entered after copying", WIZARD_MUTED);
		if (source_form) {
			wizard_frame_text(&f, 10, 26, "Install without a network connection.", WIZARD_MUTED);
			wizard_frame_text(&f, 14, 26, "Choose the URL of a matching build.", WIZARD_MUTED);
			wizard_frame_text(&f, 17, 22, "[ Custom server & build path ]",
			    pos == 2 ? WIZARD_SELECTED : WIZARD_AMBER);
			wizard_frame_text(&f, 18, 22, "[ View the recent command log ]",
			    pos == 3 ? WIZARD_SELECTED : WIZARD_AMBER);
		}
		wizard_frame_text(&f, 21, 22, "[ Back ]",
		    focus == 1 ? WIZARD_SELECTED : WIZARD_AMBER);
		wizard_frame_text(&f, 21, 58, "[ Continue ]",
		    focus == 1 ? WIZARD_AMBER : WIZARD_SELECTED);
		if (source_error[0])
			wizard_frame_text(&f, 19, 22, source_error, WIZARD_RED);
		else if (stage == 1 && boot_medium[0]) {
			int media_row = count > 1 ? 14 : 12;
			wizard_frame_text(&f, media_row, 22, boot_medium, WIZARD_DISABLED);
			wizard_frame_text(&f, media_row + 1, 26,
			    "Boot media - not an installation target", WIZARD_MUTED);
		}
		if (stage == 1) {
			wizard_frame_text(&f, 10, 26, "Target disk", WIZARD_TEXT);
			wizard_frame_text(&f, 16, 22, "Layout     Automatic / GPT", WIZARD_TEXT);
			wizard_frame_text(&f, 18, 22, "[ A: Advanced partitioning ]", WIZARD_AMBER);
			wizard_frame_text(&f, 19, 22, "The selected disk is erased after confirmation.", WIZARD_YELLOW);
		}
		draw(&f);
		key = getch();
		if (key == 27 || key == KEY_LEFT)
			return -1;
		if (!screen_ready())
			continue;
		if (stage == 1 && (key == 'a' || key == 'A')) {
			wizard_expert_requested = true;
			return -1;
		}
		if (key == '\n' || key == '\r' || key == KEY_ENTER)
			return focus == 1 ? -1 :
			    (settings_form && focus == 2 ? 4 : pos);
		if (key == '\t') {
			focus = (focus + 1) % 3;
			continue;
		}
		if (key == KEY_UP)
			pos = wizard_menu_move(pos, source_form ? 4 : count, -1);
		if (key == KEY_DOWN)
			pos = wizard_menu_move(pos, source_form ? 4 : count, 1);
	}
}

static bool
input(const char *title, char *value, size_t size)
{
	struct wizard_frame f;
	char saved[STRSIZE];
	size_t len = strlen(value);
	int key, top, left;

	strlcpy(saved, value, sizeof(saved));
	for (;;) {
		frame(&f, title);
		wizard_frame_text(&f, 9, 22, value + (len > 52 ? len - 52 : 0),
		    WIZARD_SELECTED);
		wizard_frame_text(&f, 12, 22, "Ctrl-U: clear the field", WIZARD_MUTED);
		wizard_frame_text(&f, 19, 22, source_error, WIZARD_RED);
		draw(&f);
		curs_set(1);
		if (wizard_frame_origin(getmaxy(stdscr), getmaxx(stdscr), &top, &left))
			move(top + 9, left + 22 + (len < 52 ? (int)len : 52));
		refresh();
		key = getch();
		if (!screen_ready() && key != 27)
			continue;
		if (key == '\n' || key == '\r' || key == KEY_ENTER) {
			curs_set(0);
			return true;
		}
		if (key == 27) {
			strlcpy(value, saved, size);
			curs_set(0);
			return false;
		}
		if (key == 21)
			len = 0;
		else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
			if (len > 0)
				len--;
		} else if (key >= ' ' && key <= '~' && len + 1 < size)
			value[len++] = key;
		value[len] = '\0';
	}
}

int
wizard_intro(void)
{
	struct wizard_frame f;
	int key;

#ifndef MACH_amd64
	return 0;
#endif
	custom_palette = wizard_console_setup();
	if (!screen_ready())
		return 0;
	for (;;) {
		memset(&f, 0, sizeof(f));
		for (int y = 0; y < WIZARD_ROWS; y++)
			memset(f.text[y], ' ', WIZARD_COLS);
		wizard_frame_logo(&f);
		wizard_frame_text(&f, 0, 3, "EMBERBSD " EMBER_VERSION,
		    WIZARD_AMBER);
		wizard_frame_text(&f, 0, 62, "INSTALLATION", WIZARD_MUTED);
		for (int x = 3; x < 77; x++) {
			wizard_frame_text(&f, 1, x, "-", WIZARD_EDGE);
			wizard_frame_text(&f, 22, x, "-", WIZARD_EDGE);
		}
		wizard_frame_text(&f, 2, 3, "Welcome. Let us set up your system.", WIZARD_STRONG);
		wizard_frame_text(&f, 18, 4,
		    "Choose settings. Review changes. Install.", WIZARD_MUTED);
		wizard_frame_text(&f, 20, 3, "[ Start installation ]", WIZARD_SELECTED);
		wizard_frame_text(&f, 20, 40, "[ A: Recovery & advanced ]", WIZARD_AMBER);
		wizard_frame_text(&f, 23, 3, "Enter Start    A Advanced    Q Exit", WIZARD_MUTED);
		wizard_frame_text(&f, 24, 3, "Build: " EMBER_BUILD_ID, WIZARD_MUTED);
		draw(&f);
		key = getch();
		if (key == '\n' || key == '\r' || key == KEY_ENTER)
			return 1;
		if (key == 'a' || key == 'A' || key == 27)
			return 0;
		if (key == 'q' || key == 'Q')
			return -1;
	}
}

int
wizard_initial_choice(const char *title, const char * const *names, int count, int first)
{
	stage = 0;
	int rv = choose(title, names, count, first);
	wizard_initial_back = rv < 0;
	return rv;
}

void
wizard_release_console(void)
{
	desktop_clear();
	wizard_console_restore();
	custom_palette = false;
	screen_ready();
	clear();
	refresh();
}

int
wizard_disk_choice(const char * const *names, int count)
{
	return choose("Where should EmberBSD be installed?", names, count, 0);
}

void
wizard_record_boot_medium(const char *description)
{
	strlcpy(boot_medium, description, sizeof(boot_medium));
}

bool
wizard_disk_is_root(const char *dev)
{
	int mib[2] = { CTL_KERN, KERN_ROOT_DEVICE };
	char rootdev[SSTRSIZE], node[STRSIZE];
	size_t size = sizeof(rootdev);
	struct dkwedge_info wedge;
	int fd, rv;

	if (sysctl(mib, 2, rootdev, &size, NULL, 0) != 0)
		return true;
	rootdev[sizeof(rootdev) - 1] = '\0';
	if (strncmp(rootdev, "dk", 2) != 0)
		return strcmp(rootdev, dev) == 0;
	if (strcmp(rootdev, dev) == 0)
		return true;
	snprintf(node, sizeof(node), "/dev/r%s", rootdev);
	fd = open(node, O_RDONLY);
	if (fd < 0)
		return true;
	rv = ioctl(fd, DIOCGWEDGEINFO, &wedge);
	close(fd);
	if (rv != 0)
		return true;
	wedge.dkw_parent[sizeof(wedge.dkw_parent) - 1] = '\0';
	return strcmp(wedge.dkw_parent, dev) == 0;
}

static int
select_source(void)
{
	static const char * const options[] = {
		"Bootable USB / installation image", "EmberBSD CDN / network",
		"Custom server & build path", "View the recent command log", "Back"
	};
	int choice = choose("Choose your installation source", options, 5,
	    net_source ? 1 : 0);
	if (choice < 0 || choice == 4)
		return SET_ABANDON;
	if (choice == 3) {
		char lines[9][STRSIZE], line[STRSIZE];
		const char *entries[10];
		int count = 0;
		FILE *fp = fopen("/tmp/sysinst.log", "r");
		if (fp != NULL) {
			while (fgets(line, sizeof(line), fp) != NULL) {
				line[strcspn(line, "\r\n")] = '\0';
				if (count == 9) {
					memmove(lines, lines + 1, sizeof(lines[0]) * 8);
					count--;
				}
				strlcpy(lines[count++], line, sizeof(lines[0]));
			}
			fclose(fp);
		}
		for (int n = 0; n < count; n++)
			entries[n] = lines[n];
		entries[count++] = "Return to source selection";
		choose("Recent command details", entries, count, count - 1);
		return SET_RETRY;
	}
	source_error[0] = '\0';
	net_source = choice != 0;
	fetch_fn = NULL;
	if (!net_source) {
		strlcpy(set_dir_bin, "/" ARCH_SUBDIR "/binary/sets", sizeof(set_dir_bin));
		strlcpy(set_dir_src, "/source/sets", sizeof(set_dir_src));
		return get_via_cdrom();
	}
	if (source_url[0] == '\0') {
		const char *prefix = SYSINST_FTP_SET_DIR_BIN;
		while (*prefix == '/')
			prefix++;
		snprintf(source_url, sizeof(source_url),
		    "https://%s/%s%s%s", SYSINST_HTTP_HOST,
		    SYSINST_FTP_DIR, *prefix ? "/" : "", prefix);
	}
	if (!input("Set directory URL (an immutable build)", source_url,
	    sizeof(source_url)))
		return SET_RETRY;
	int rv = wizard_network_source(source_url);
	if (rv != SET_OK)
		strlcpy(source_error, "Check URL/network and HTTPS certificate support.",
		    sizeof(source_error));
	return rv;
}

static bool
desktop_clear(void)
{
	if (desktop_bundle[0])
		unlink(desktop_bundle);
	desktop_bundle[0] = '\0';
	desktop_sum[0] = '\0';
	if (desktop_mounted) {
		if (run_program(RUN_SILENT | RUN_ERROR_OK,
		    "/sbin/umount %s", desktop_cache) != 0)
			return false;
		desktop_mounted = false;
	}
	if (desktop_cache[0])
		rmdir(desktop_cache);
	desktop_cache[0] = '\0';
	return true;
}

/* Freeze and verify all optional package bytes before any disk writes. */
static bool
desktop_preflight(const char *manifest)
{
	char expected[129], actual[129], path[STRSIZE];
	const char *bundle_name;
	unsigned char buffer[16384];
	struct stat st;
	struct statvfs fs;
	FILE *fp, *input = NULL, *output = NULL;
	size_t n, total = 0;
	bool ok = false;
	int fd;

	if (!desktop_clear())
		return false;
	if (desktop_index == 0)
		return true;
	if (desktop_index == 1)
		bundle_name = "desktop-xfce.tar.xz";
	else if (desktop_index == 2)
		bundle_name = "desktop-plasma.tar.xz";
	else
		return false;
	fp = fopen(manifest, "r");
	if (fp == NULL)
		return false;
	int match = wizard_checksum_entry(fp, bundle_name, expected);
	fclose(fp);
	if (match != 1)
		return false;
	strlcpy(desktop_cache, "/tmp/ember-desktop-cache.XXXXXX", sizeof(desktop_cache));
	if (mkdtemp(desktop_cache) == NULL) {
		desktop_cache[0] = '\0';
		return false;
	}
	/* ISO miniroots are small. Network staging also needs a hard size cap. */
	if (net_source || statvfs("/tmp", &fs) != 0 || fs.f_frsize == 0 ||
	    fs.f_bavail < 512 * MEG / fs.f_frsize) {
		if (run_program(RUN_SILENT | RUN_ERROR_OK,
		    "/sbin/mount_tmpfs -s 536870912 -o nosuid,nodev tmpfs %s",
		    desktop_cache) != 0)
			goto out;
		desktop_mounted = true;
	}
	snprintf(desktop_bundle, sizeof(desktop_bundle), "%s/bundle.XXXXXX", desktop_cache);
	fd = mkstemp(desktop_bundle);
	if (fd < 0) {
		desktop_bundle[0] = '\0';
		goto out;
	}
	close(fd);
	if (net_source) {
		if (run_program(RUN_SILENT | RUN_ERROR_OK,
		    "/usr/bin/ftp -q 30 -o %s %s/%s",
		    desktop_bundle, source_url, bundle_name) != 0)
			goto out;
	} else {
		snprintf(path, sizeof(path), "%s/%s", ext_dir_bin, bundle_name);
		fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			goto out;
		if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
		    st.st_size < 1 || st.st_size > (off_t)(512 * MEG)) {
			close(fd);
			goto out;
		}
		input = fdopen(fd, "rb");
		if (input == NULL) {
			close(fd);
			goto out;
		}
		output = fopen(desktop_bundle, "wb");
		if (output == NULL)
			goto out;
		for (;;) {
			n = fread(buffer, 1, sizeof(buffer), input);
			if ((ferror)(input))
				goto out;
			if (n == 0)
				break;
			total += n;
			if (total > 512 * MEG || fwrite(buffer, 1, n, output) != n)
				goto out;
			if ((feof)(input))
				break;
		}
		int error = fclose(output);
		output = NULL;
		if (error != 0)
			goto out;
	}
	ok = stat(desktop_bundle, &st) == 0 && S_ISREG(st.st_mode) &&
	    st.st_size > 0 && st.st_size <= (off_t)(512 * MEG) &&
	    SHA512_File(desktop_bundle, actual) != NULL &&
	    strcmp(expected, actual) == 0;
out:
	if (input != NULL)
		fclose(input);
	if (output != NULL)
		fclose(output);
	if (ok)
		strlcpy(desktop_sum, expected, sizeof(desktop_sum));
	else
		desktop_clear();
	return ok;
}

static bool
preflight(void)
{
	char idpath[STRSIZE], sums[STRSIZE], id[128];
	char *newline;
	FILE *fp;
	struct stat st;
	bool ok = false;
	int fd;

	wizard_progress("Checking source metadata before disk writes");
	if (net_source) {
		strlcpy(idpath, "/tmp/ember-build-id.XXXXXX", sizeof(idpath));
		strlcpy(sums, "/tmp/ember-checksums.XXXXXX", sizeof(sums));
		fd = mkstemp(idpath);
		if (fd < 0)
			return false;
		close(fd);
		fd = mkstemp(sums);
		if (fd < 0) {
			unlink(idpath);
			return false;
		}
		close(fd);
		if (run_program(RUN_SILENT | RUN_ERROR_OK,
		    "/usr/bin/ftp -q 20 -o %s %s/BUILDID", idpath, source_url) != 0 ||
		    run_program(RUN_SILENT | RUN_ERROR_OK,
		    "/usr/bin/ftp -q 20 -o %s %s/SHA512", sums, source_url) != 0)
			goto out;
	} else {
		snprintf(idpath, sizeof(idpath), "%s/BUILDID", ext_dir_bin);
		snprintf(sums, sizeof(sums), "%s/SHA512", ext_dir_bin);
	}
	fp = fopen(idpath, "r");
	if (fp == NULL)
		goto out;
	if (fgets(id, sizeof(id), fp) == NULL) {
		fclose(fp);
		goto out;
	}
	fclose(fp);
	newline = strchr(id, '\n');
	if (newline)
		*newline = '\0';
	if (strcmp(id, EMBER_BUILD_ID) != 0 ||
	    stat(sums, &st) != 0 || st.st_size > 1024 * 1024)
		goto out;
	ok = wizard_check_sets(sums) == SET_OK;
	if (ok)
		ok = desktop_preflight(sums);
out:
	if (net_source) {
		unlink(idpath);
		unlink(sums);
	}
	if (!ok)
		strlcpy(source_error, "Build check failed. Retry or choose another source.",
		    sizeof(source_error));
	return ok;
}

int
wizard_retry_source(void)
{
	int oldstage = stage;
	stage = 2;
	for (;;) {
		int rv = select_source();
		if (rv == SET_ABANDON) {
			stage = oldstage;
			return rv;
		}
		if (rv == SET_OK && preflight()) {
			stage = oldstage;
			return SET_OK;
		}
	}
}

static bool
settings(void)
{
	static const char * const zones[] = {
		"UTC", "Europe/Moscow", "Europe/Berlin", "America/New_York",
		"Asia/Tokyo", "Back"
	};
	char hostline[80], userline[80], tzline[80], netline[80];
	const char *options[5] = { hostline, tzline, netline, userline,
	    "Continue to review" };
	for (;;) {
		snprintf(hostline, sizeof(hostline), "Computer name    %s", hostname);
		snprintf(tzline, sizeof(tzline), "Time zone        %s", wizard_timezone);
		snprintf(netline, sizeof(netline), "Network          %s",
		    network_up ? "configured" : "optional for USB");
		snprintf(userline, sizeof(userline), "Local account    %s",
		    username[0] ? username : "none (root console)");
		int choice = choose("Make this system yours", options, 5, 4);
		if (choice < 0)
			return false;
		if (choice == 4)
			return true;
		if (choice == 0) {
			char val[sizeof(hostname)];
			strlcpy(val, hostname, sizeof(val));
			while (input("Hostname (lowercase letters, digits and hyphens)", val,
			    sizeof(val))) {
				if (wizard_valid_name(val, 0)) {
					strlcpy(hostname, val, sizeof(hostname));
					break;
				}
				strlcpy(source_error, "Use a valid lowercase hostname.", sizeof(source_error));
			}
			source_error[0] = '\0';
		} else if (choice == 1) {
			int z = choose("Choose a timezone", zones, 6, 0);
			if (z >= 0 && z < 5)
				strlcpy(wizard_timezone, zones[z], sizeof(wizard_timezone));
		} else if (choice == 3) {
			char val[sizeof(username)];
			strlcpy(val, username, sizeof(val));
			while (input("Local username (empty: root console only)", val,
			    sizeof(val))) {
				if (val[0] == '\0' || (wizard_valid_name(val, 1) &&
				    getpwnam(val) == NULL)) {
					strlcpy(username, val, sizeof(username));
					break;
				}
				strlcpy(source_error, "Use a new lowercase username (not a system account).",
				    sizeof(source_error));
			}
			source_error[0] = '\0';
		} else {
			network_up = 0;
			config_network(0);
		}
	}
}

static bool
layout(struct install_partition_desc *install)
{
	wizard_automatic = true;
	if (wizard_automatic) {
#if defined(HAVE_GPT) && defined(MACH_amd64)
		extern const struct disk_partitioning_scheme gpt_parts;
		struct disk_partitions *parts = gpt_parts.create_new_for_disk(
		    pm->diskdev, 0, pm->dlsize, true, NULL);
		if (parts == NULL)
			return false;
		if (pm->parts != NULL)
			pm->parts->pscheme->free(pm->parts);
		pm->parts = parts;
		pm->ptstart = 0;
		pm->ptsize = pm->dlsize;
#else
		return false;
#endif
	}
	return md_get_info(install) && md_make_bsd_partitions(install) == 1;
}

static int
review(const struct install_partition_desc *install)
{
	struct wizard_frame f;
	char text[128];
	int key;

	frame(&f, "Ready to install");
	wizard_frame_text(&f, 6, 22, "Check the target and the changes below.", WIZARD_MUTED);
	snprintf(text, sizeof(text), "Target       %s", pm->diskdev_descr);
	wizard_frame_text(&f, 9, 22, text, WIZARD_TEXT);
	snprintf(text, sizeof(text), "Layout       %s", msg_string(pm->parts->pscheme->short_name));
	for (size_t i = 0; i < install->num; i++) {
		const struct part_usage_info *p = &install->infos[i];
		char part[64];
		if (p->size == 0)
			continue;
		if (p->size < 0)
			snprintf(part, sizeof(part), " / %s %" PRIu64 "%% RAM",
			    p->mount, (uint64_t)(-p->size));
		else
			snprintf(part, sizeof(part), " / %s %" PRIu64 " MiB",
			    p->type == PT_EFI_SYSTEM ? "EFI" : (p->mount[0] ?
			    p->mount : getfslabelname(p->fs_type, p->fs_version)),
			    (uint64_t)p->size * pm->sectorsize / MEG);
		strlcat(text, part, sizeof(text));
	}
	/* Keep the partition plan visible within the 53-column content area. */
	if (strlen(text) > 53) {
		size_t split = 13;
		for (size_t i = 13; i <= 53; i++) {
			if (strncmp(text + i, " / ", 3) == 0)
				split = i;
		}
		char next[sizeof(text)];
		strlcpy(next, text + split + 3, sizeof(next));
		text[split] = '\0';
		wizard_frame_text(&f, 12, 22, next, WIZARD_MUTED);
	}
	wizard_frame_text(&f, 11, 22, text, WIZARD_TEXT);
	if (wizard_optional_omissions()) {
		snprintf(text, sizeof(text), "Optional compatibility sets omitted: %u",
		    wizard_optional_omissions());
		wizard_frame_text(&f, 20, 22, text, WIZARD_MUTED);
	}
	if (net_source) {
		char host[STRSIZE], path[STRSIZE];
		int secure;
		if (wizard_parse_url(source_url, host, sizeof(host), path,
		    sizeof(path), &secure)) {
			snprintf(text, sizeof(text), "Source       %s / %s", secure ? "HTTPS" : "HTTP", host);
			wizard_frame_text(&f, 13, 22, text, WIZARD_TEXT);
		}
	} else
		wizard_frame_text(&f, 13, 22, "Source       USB / installation image", WIZARD_TEXT);
	wizard_frame_text(&f, 14, 22, "Build        " EMBER_BUILD_ID, WIZARD_MUTED);
	snprintf(text, sizeof(text), "System       %s", profile);
	wizard_frame_text(&f, 15, 22, text, WIZARD_TEXT);
	wizard_frame_text(&f, 16, 22, desktop_index == 2 ?
	    "Desktop      KDE Plasma / experimental" : desktop_index ?
	    "Desktop      XFCE / Wi-Fi manager" :
	    "Desktop      None / console Wi-Fi", WIZARD_TEXT);
	snprintf(text, sizeof(text), "Computer     %s", hostname);
	wizard_frame_text(&f, 17, 22, text, WIZARD_TEXT);
	snprintf(text, sizeof(text), "%s / user: %s", wizard_timezone,
	    username[0] ? username : "none");
	wizard_frame_text(&f, 18, 22, text, WIZARD_MUTED);
	wizard_frame_text(&f, 19, 22,
	    "ALL DATA ON THE TARGET DISK WILL BE ERASED.", WIZARD_YELLOW);
	wizard_frame_text(&f, 21, 56, "                       ", WIZARD_TEXT);
	wizard_frame_text(&f, 21, 56, "[ I: Install system ]", WIZARD_SELECTED);
	wizard_frame_text(&f, 23, 3,
	    "I Install    Esc Back    Passwords are entered after copying.     ", WIZARD_MUTED);
	for (;;) {
		bool ready = screen_ready();
		draw(&f);
		key = getch();
		if (ready && screen_ready() &&
		    wizard_can_confirm(key, getmaxy(stdscr), getmaxx(stdscr)))
			return 1;
		if (key == 27 || key == KEY_LEFT)
			return 0;
	}
}

bool
wizard_prepare(struct install_partition_desc *install)
{
	static const char * const desktops[] = {
		"None - console only", "XFCE with Wi-Fi manager",
		"KDE Plasma - experimental"
	};
	stage = 1;
	wizard_expert_requested = false;
	boot_medium[0] = '\0';
	profile_ready = false;
	wizard_installing = false;
	for (;;) {
		if (stage == 1) {
			free_install_desc(install);
			memset(install, 0, sizeof(*install));
			partman_go = 0;
			if (find_disks("install", false) < 0)
				return false;
			if (!layout(install))
				continue;
			stage = 2;
		} else if (stage == 2) {
			int rv = select_source();
			if (rv == SET_ABANDON)
				stage = 1;
			else if (rv == SET_OK)
				stage = 3;
		} else if (stage == 3) {
			char graphics[64];
			snprintf(graphics, sizeof(graphics), "[ Graphics: %s ]",
			    desktop_index == 2 ? "KDE Plasma" :
			    (desktop_index ? "XFCE + Wi-Fi" : "None"));
			const char *profiles[] = {
				"Standard system", "Minimal system", "Custom components", graphics
			};
			int p = choose("Choose system and graphics", profiles, 4, profile_index);
			if (p == 3) {
				int d = choose("Choose your graphics environment", desktops, 3, desktop_index);
				if (d >= 0 && d < 3)
					desktop_index = d;
				continue;
			}
			if (p < 0) {
				stage = 2;
				continue;
			}
			if (!profile_ready || p != profile_index)
				init_set_status(p == 0 ? SFLAG_NOX : SFLAG_MINIMAL);
			profile_index = p;
			profile_ready = true;
			wizard_custom = p == 2;
			wizard_limit_sets();
			if (p == 2)
				customise_sets();
			wizard_desktop_sets(desktop_index != 0);
			profile = p == 0 ? (desktop_index ? "Standard" : "Standard (without X11)") :
			    (p == 1 ? "Minimal" : "Custom");
			stage = 4;
		} else if (stage == 4) {
			stage = settings() ? 5 : 3;
		} else {
			if (!preflight()) {
				stage = 2;
				continue;
			}
			if (review(install))
				return true;
			stage = 4;
		}
	}
}

void
wizard_progress(const char *what)
{
	struct wizard_frame f;
	activity = what;
	frame(&f, wizard_installing ? "Installing EmberBSD" : "Checking installation source");
	wizard_frame_text(&f, 6, 22, what, WIZARD_AMBER);
	wizard_frame_text(&f, 19, 22, "Details are recorded in /tmp/sysinst.log.", WIZARD_MUTED);
	wizard_frame_text(&f, 21, 22, "Keep the installation source connected.           ", WIZARD_TEXT);
	draw(&f);
}

WINDOW *
wizard_command_window(void)
{
	WINDOW *win;
	int top, left;
	wizard_progress(activity);
	if (!wizard_frame_origin(getmaxy(stdscr), getmaxx(stdscr), &top, &left))
		return NULL;
	win = subwin(stdscr, 10, 53, top + 9, left + 22);
	if (win != NULL) {
		scrollok(win, TRUE);
		wbkgd(win, has_colors() ? COLOR_PAIR(3) : A_NORMAL);
	}
	return win;
}

bool
wizard_configure(void)
{
	char path[STRSIZE], zone[STRSIZE];
	static const char * const done[] = { "Finish installation", "Advanced settings" };

	wizard_progress("Applying the selected system settings");
	if (network_up)
		mnt_net_config();
	if (desktop_index != 0) {
		char sum[129];
		make_target_dir("/var/cache/ember-desktop");
		if (desktop_bundle[0] == '\0' ||
		    cp_to_target(desktop_bundle, "/var/cache/ember-desktop.tar.xz") != 0 ||
		    SHA512_File(target_expand("/var/cache/ember-desktop.tar.xz"), sum) == NULL ||
		    strcmp(sum, desktop_sum) != 0 ||
		    run_program(RUN_PROGRESS | RUN_CHROOT,
		    "/usr/bin/tar -xpf /var/cache/ember-desktop.tar.xz -C /var/cache/ember-desktop") != 0 ||
		    run_program(RUN_PROGRESS | RUN_CHROOT,
		    "/usr/libexec/ember-desktop-install %s",
		    desktop_index == 2 ? "plasma" : "xfce") != 0)
			return false;
		unlink(target_expand("/var/cache/ember-desktop.tar.xz"));
		desktop_clear();
	}
	if (del_rc_conf("hostname") != 0)
		return false;
	add_rc_conf("hostname=%s\n", hostname);
	strlcpy(zone, target_expand("/usr/share/zoneinfo/"), sizeof(zone));
	strlcat(zone, wizard_timezone, sizeof(zone));
	if (!file_exists_p(zone))
		return false;
	strlcpy(path, target_expand("/etc/localtime"), sizeof(path));
	snprintf(zone, sizeof(zone), "/usr/share/zoneinfo/%s", wizard_timezone);
	if ((unlink(path) != 0 && errno != ENOENT) || symlink(zone, path) != 0)
		return false;
	if (username[0]) {
		if (run_program(RUN_PROGRESS | RUN_CHROOT,
		    "/usr/sbin/useradd -m -s /bin/sh -G wheel %s", username) != 0 ||
		    run_program(RUN_DISPLAY | RUN_PROGRESS | RUN_CHROOT,
		    "passwd -l %s", username) != 0)
			return false;
	}
	for (;;) {
		int choice = choose("System copied and configured", done, 2, 0);
		if (choice == 0)
			return true;
		if (choice == 1)
			do_configmenu(NULL);
	}
}
