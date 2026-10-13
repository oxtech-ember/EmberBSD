/* Origin: EmberBSD text installer frame and input policy, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <string.h>
#include "wizard_ui.h"

/* Use the same generated logo data as the boot loaders. */
#include "../../sys/lib/libsa/ember_logo_data.h"

/* The approved flame palette; high-intensity text retains these hues. */
const unsigned char wizard_console_rgb[16][3] = {
	{ 0x10, 0x11, 0x10 }, { 0xff, 0x46, 0x3b },
	{ 0x9f, 0x9a, 0x90 }, { 0xff, 0xab, 0x42 },
	{ 0x43, 0x44, 0x3d }, { 0x68, 0x69, 0x61 },
	{ 0xff, 0xe4, 0x66 }, { 0xee, 0xe8, 0xde },
	{ 0x10, 0x11, 0x10 }, { 0xff, 0x46, 0x3b },
	{ 0x9f, 0x9a, 0x90 }, { 0xff, 0xab, 0x42 },
	{ 0x43, 0x44, 0x3d }, { 0x68, 0x69, 0x61 },
	{ 0xff, 0xe4, 0x66 }, { 0xee, 0xe8, 0xde }
};

void
wizard_frame_text(struct wizard_frame *f, int y, int x, const char *s,
    int color)
{
	if (y < 0 || y >= WIZARD_ROWS || x < 0 || x >= WIZARD_COLS - 1)
		return;
	while (*s != '\0' && x < WIZARD_COLS - 1) {
		unsigned char ch = *s++;
		f->text[y][x] = ch >= ' ' && ch <= '~' ? ch : '?';
		f->color[y][x++] = color;
	}
}

void
wizard_frame_init(struct wizard_frame *f, int step, const char *title)
{
	static const char * const steps[] = {
		"LANGUAGE", "DISK", "SOURCE", "SYSTEM", "SETTINGS", "REVIEW"
	};
	char line[32];

	memset(f, 0, sizeof(*f));
	for (int y = 0; y < WIZARD_ROWS; y++) {
		memset(f->text[y], ' ', WIZARD_COLS);
		f->text[y][WIZARD_COLS] = '\0';
	}
	wizard_frame_text(f, 0, 3, "EMBERBSD", WIZARD_AMBER);
	wizard_frame_text(f, 0, 62, "INSTALLATION", WIZARD_MUTED);
	for (int x = 3; x < 77; x++) {
		wizard_frame_text(f, 1, x, "-", WIZARD_EDGE);
		wizard_frame_text(f, 22, x, "-", WIZARD_EDGE);
	}
	if (step >= 0 && step < 6)
		snprintf(line, sizeof(line), "STEP %d OF 6", step + 1);
	else
		strcpy(line, "INSTALLATION");
	wizard_frame_text(f, 2, 3, line, WIZARD_AMBER);
	if (step >= 0 && step < 6)
		wizard_frame_text(f, 2, 44, "No changes made to the disk yet.", WIZARD_MUTED);
	for (int y = 4; y <= 20; y++)
		wizard_frame_text(f, y, 19, "|", WIZARD_EDGE);
	wizard_frame_text(f, 4, 22, title, WIZARD_STRONG);
	for (int i = 0; i < 6; i++) {
		snprintf(line, sizeof(line), "%c %02d %-9s",
		    i == step ? '>' : (i < step ? '+' : ' '), i + 1, steps[i]);
		wizard_frame_text(f, 6 + i * 2, 3, line,
		    i == step ? WIZARD_SELECTED : (i < step ? WIZARD_TEXT : WIZARD_MUTED));
	}
	wizard_frame_text(f, 21, 22, "[ Back ]", WIZARD_AMBER);
	wizard_frame_text(f, 21, 58, "[ Continue ]", WIZARD_SELECTED);
	wizard_frame_text(f, 23, 3,
	    "Tab Move    Arrows Select    Enter Continue    Esc Back", WIZARD_MUTED);
}

void
wizard_frame_choice(struct wizard_frame *f, int row, const char *text, int selected)
{
	char line[54];
	snprintf(line, sizeof(line), "(%c) %-49.49s", selected ? 'o' : ' ', text);
	wizard_frame_text(f, row, 22, line, selected ? WIZARD_SELECTED : WIZARD_TEXT);
}

int
wizard_frame_origin(int rows, int cols, int *top, int *left)
{
	if (rows < WIZARD_ROWS || cols < WIZARD_COLS)
		return 0;
	*top = (rows - WIZARD_ROWS) / 2;
	*left = (cols - WIZARD_COLS) / 2;
	return 1;
}

void
wizard_frame_logo(struct wizard_frame *f)
{
	for (size_t y = 0; y < sizeof(ember_logo_mascot) /
	    sizeof(ember_logo_mascot[0]); y++) {
		for (size_t x = 0; ember_logo_mascot[y][x] != '\0'; x++) {
			char ch = ember_logo_mascot[y][x], cell[2] = { ch, 0 };
			int color = WIZARD_TEXT;
			if (ch == 'r')
				color = WIZARD_RED, cell[0] = '#';
			else if (ch == '+')
				color = WIZARD_AMBER, cell[0] = '#';
			else if (ch == '#')
				color = WIZARD_YELLOW;
			wizard_frame_text(f, 4 + (int)y, 2 + (int)x, cell, color);
		}
		if (y >= 4 && y < 11)
			wizard_frame_text(f, 4 + (int)y, 31,
			    ember_logo_wordmark[y - 4], WIZARD_TEXT);
	}
}

int
wizard_parse_url(const char *url, char *host, size_t hlen, char *path,
    size_t plen, int *https)
{
	const char *p, *slash;
	size_t n;
	int secure;

	if (strncmp(url, "https://", 8) == 0)
		p = url + 8, secure = 1;
	else if (strncmp(url, "http://", 7) == 0)
		p = url + 7, secure = 0;
	else
		return 0;
	slash = strchr(p, '/');
	if (slash == NULL || slash == p || strstr(slash, "..") != NULL)
		return 0;
	n = (size_t)(slash - p);
	if (n >= hlen || strlen(slash) >= plen)
		return 0;
	for (const char *s = p; s < slash; s++)
		if (!((*s >= 'a' && *s <= 'z') ||
		    (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
		    *s == '.' || *s == '-' || *s == ':'))
			return 0;
	for (const char *s = slash; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') ||
		    (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
		    strchr("/._-", *s) != NULL))
			return 0;
	memcpy(host, p, n);
	host[n] = '\0';
	memcpy(path, slash, strlen(slash) + 1);
	*https = secure;
	return 1;
}

int
wizard_valid_name(const char *s, int user)
{
	size_t len = strlen(s);
	if (len == 0 || len > (user ? 31U : 63U) ||
	    !((*s >= 'a' && *s <= 'z') || (!user && *s >= '0' && *s <= '9')))
		return 0;
	for (; *s; s++)
		if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') ||
		    *s == '-' || (user && *s == '_')))
			return 0;
	return 1;
}

int
wizard_menu_move(int choice, int count, int delta)
{
	if (count <= 0)
		return -1;
	return (choice + delta + count) % count;
}

int
wizard_checksum_entry(FILE *fp, const char *name, char sum[129])
{
	char line[1024], prefix[256];
	size_t len;

	if (snprintf(prefix, sizeof(prefix), "SHA512 (%s) = ", name) >=
	    (int)sizeof(prefix))
		return 0;
	len = strlen(prefix);
	if (fseek(fp, 0, SEEK_SET) != 0)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strncmp(line, prefix, len) != 0)
			continue;
		if (strlen(line + len) != 129 || line[len + 128] != '\n')
			return -1;
		for (int i = 0; i < 128; i++) {
			char ch = line[len + i];
			if (ch >= 'A' && ch <= 'F')
				ch += 'a' - 'A';
			if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
				return -1;
			sum[i] = ch;
		}
		sum[128] = '\0';
		return 1;
	}
	return 0;
}

int
wizard_can_confirm(int key, int rows, int cols)
{
	return (key == 'i' || key == 'I') && rows >= WIZARD_ROWS &&
	    cols >= WIZARD_COLS;
}
