/* Origin: EmberBSD text installer contracts, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "wizard_ui.h"

int
main(void)
{
	struct wizard_frame f;
	char host[128], path[256];
	int https;
	char sum[129];
	FILE *manifest = tmpfile();
	assert(manifest != NULL);
	fprintf(manifest, "SHA512 (base.tar.xz) = %0128d\n", 0);
	fprintf(manifest, "SHA512 (bad.tar.xz) = short\n");
	rewind(manifest);
	assert(wizard_checksum_entry(manifest, "base.tar.xz", sum));
	assert(strlen(sum) == 128);
	assert(wizard_checksum_entry(manifest, "bad.tar.xz", sum) == -1);
	assert(!wizard_checksum_entry(manifest, "kern-GENERIC.tar.xz", sum));
	fclose(manifest);
	int pipes[2];
	assert(pipe(pipes) == 0);
	char entry[256];
	int n = snprintf(entry, sizeof(entry), "SHA512 (base.tar.xz) = %0128d\n", 0);
	assert(write(pipes[1], entry, (size_t)n) == n);
	close(pipes[1]);
	manifest = fdopen(pipes[0], "r");
	assert(manifest != NULL);
	assert(wizard_checksum_entry(manifest, "base.tar.xz", sum) == -1);
	fclose(manifest);

	wizard_frame_init(&f, 2, "Choose an installation source");
	assert(strncmp(f.text[0] + 3, "EMBERBSD", 8) == 0);
	assert(strncmp(f.text[0] + 62, "INSTALLATION", 12) == 0);
	assert(strncmp(f.text[2] + 3, "STEP 3 OF 6", 11) == 0);
	assert(strncmp(f.text[10] + 3, "> 03 SOURCE", 11) == 0);
	for (int y = 4; y <= 20; y++)
		assert(f.text[y][19] == '|');
	for (int x = 3; x < 77; x++) {
		assert(f.text[1][x] == '-');
		assert(f.text[22][x] == '-');
	}
	assert(strncmp(f.text[4] + 22, "Choose an installation source", 29) == 0);
	assert(strncmp(f.text[21] + 58, "[ Continue ]", 12) == 0);
	assert(strncmp(f.text[23] + 3, "Tab Move", 8) == 0);
	for (int x = 3; x < 17; x++)
		assert(f.color[10][x] == WIZARD_SELECTED);
	assert(wizard_console_rgb[3][0] == 255 && wizard_console_rgb[3][1] == 171 &&
	    wizard_console_rgb[3][2] == 66);
	assert(wizard_console_rgb[0][0] == 16 && wizard_console_rgb[0][1] == 17);
	int top, left;
	assert(wizard_frame_origin(25, 80, &top, &left) && top == 0 && left == 0);
	assert(wizard_frame_origin(32, 85, &top, &left) && top == 3 && left == 2);
	assert(!wizard_frame_origin(24, 80, &top, &left));
	wizard_frame_choice(&f, 9, "wd0 / 32 GiB / QEMU HARDDISK", 1);
	assert(strncmp(f.text[9] + 22, "(o) wd0", 7) == 0);
	for (int x = 22; x < 75; x++)
		assert(f.color[9][x] == WIZARD_SELECTED);
	wizard_frame_text(&f, 6, 77, "abcdef", WIZARD_RED);
	assert(strcmp(f.text[6] + 77, "ab ") == 0);
	wizard_frame_text(&f, -1, 0, "bad", WIZARD_TEXT);
	wizard_frame_text(&f, 25, 0, "bad", WIZARD_TEXT);
	wizard_frame_text(&f, 5, 23, "a\033b\nc", WIZARD_TEXT);
	assert(strncmp(f.text[5] + 23, "a?b?c", 5) == 0);
	wizard_frame_logo(&f);
	assert(strstr(f.text[13], "||") != NULL);
	assert(strstr(f.text[14], "\\_/") != NULL);
	for (int y = 0; y < WIZARD_ROWS; y++) {
		assert(strlen(f.text[y]) == WIZARD_COLS);
		for (int x = 0; x < WIZARD_COLS; x++)
			assert(f.text[y][x] >= ' ' && f.text[y][x] <= '~');
	}
	assert(wizard_parse_url("https://cdn.emberbsd.oxteam.me/releases/abc/amd64/binary/sets",
	    host, sizeof(host), path, sizeof(path), &https));
	assert(https == 1 && strcmp(host, "cdn.emberbsd.oxteam.me") == 0);
	assert(strcmp(path, "/releases/abc/amd64/binary/sets") == 0);
	assert(wizard_parse_url("http://10.0.2.100/build", host,
	    sizeof(host), path, sizeof(path), &https) && https == 0);
	assert(!wizard_parse_url("http://x/$(id)", host,
	    sizeof(host), path, sizeof(path), &https));
	assert(!wizard_parse_url("http://x/a b", host,
	    sizeof(host), path, sizeof(path), &https));
	assert(!wizard_parse_url("http://u:p@host/path", host,
	    sizeof(host), path, sizeof(path), &https));
	assert(!wizard_parse_url("http://host/../../etc", host,
	    sizeof(host), path, sizeof(path), &https));
	assert(!wizard_parse_url("ftp://host/path", host,
	    sizeof(host), path, sizeof(path), &https));
	assert(!wizard_parse_url("http://host/a", host, 2,
	    path, sizeof(path), &https));
	assert(wizard_valid_name("ember-vm", 0));
	assert(wizard_valid_name("emberuser", 1));
	assert(!wizard_valid_name("-option", 1));
	assert(!wizard_valid_name("root;id", 1));
	assert(!wizard_valid_name("", 0));
	assert(wizard_menu_move(0, 3, -1) == 2);
	assert(wizard_menu_move(2, 3, 1) == 0);
	assert(wizard_can_confirm('I', 25, 80));
	assert(!wizard_can_confirm('I', 24, 80));
	assert(!wizard_can_confirm('I', 25, 79));
	assert(!wizard_can_confirm('\n', 25, 80));
	puts("PASS: bounded 80x25 ASCII frame, original eyes, input validation and menu movement");
	return 0;
}
