/* Origin: EmberBSD text installer frame and input policy, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef SYSINST_WIZARD_UI_H
#define SYSINST_WIZARD_UI_H

#include <stddef.h>
#include <stdio.h>
#define WIZARD_COLS 80
#define WIZARD_ROWS 25
enum wizard_palette {
	WIZARD_TEXT, WIZARD_MUTED, WIZARD_RED, WIZARD_AMBER,
	WIZARD_YELLOW, WIZARD_EDGE, WIZARD_STRONG, WIZARD_DISABLED,
	WIZARD_SELECTED
};
struct wizard_frame {
	char text[WIZARD_ROWS][WIZARD_COLS + 1];
	unsigned char color[WIZARD_ROWS][WIZARD_COLS];
};
void wizard_frame_init(struct wizard_frame *, int, const char *);
void wizard_frame_text(struct wizard_frame *, int, int, const char *, int);
void wizard_frame_logo(struct wizard_frame *);
void wizard_frame_choice(struct wizard_frame *, int, const char *, int);
int wizard_frame_origin(int, int, int *, int *);
extern const unsigned char wizard_console_rgb[16][3];
int wizard_parse_url(const char *, char *, size_t, char *, size_t, int *);
int wizard_valid_name(const char *, int);
int wizard_menu_move(int, int, int);
int wizard_checksum_entry(FILE *, const char *, char [129]);
int wizard_can_confirm(int, int, int);
#endif
