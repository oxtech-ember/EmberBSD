/* Origin: EmberBSD ASCII boot identity, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _LIBSA_EMBER_BOOT_LOGO_H
#define _LIBSA_EMBER_BOOT_LOGO_H

/* The optional console hook changes attributes, not the printable text. */
static inline void
ember_boot_logo(void (*color)(int))
{
	static const char * const flame[] = {
		"         /\\       ",
		"        /  \\      ",
		"     /\\/    \\     ",
		"    /        \\    ",
		"   /   /\\     \\   ",
		"   \\  /  \\    /   ",
		"    \\/    \\__/    ",
		"     \\____/       ",
	};
	static const char * const wordmark[] = {
		"",
		"#####  #   #  ####   #####  ####   ####    ####  ####",
		"#      ## ##  #   #  #      #   #  #   #  #      #   #",
		"#      # # #  #   #  #      #   #  #   #  #      #   #",
		"####   #   #  ####   ####   ####   ####    ###   #   #",
		"#      #   #  #   #  #      # #    #   #      #  #   #",
		"#      #   #  #   #  #      #  #   #   #      #  #   #",
		"#####  #   #  ####   #####  #   #  ####   ####   ####",
	};
	unsigned i;

	if (color != NULL)
		color(BOOTCFG_COLOR_SAVE);
	for (i = 0; i < sizeof(flame) / sizeof(flame[0]); i++) {
		if (color != NULL)
			color(BOOTCFG_COLOR_FLAME);
		printf("%s", flame[i]);
		if (color != NULL)
			color(BOOTCFG_COLOR_WORDMARK);
		printf("%s\n", wordmark[i]);
	}
	if (color != NULL)
		color(BOOTCFG_COLOR_RESTORE);
	printf("\n");
}

#endif /* _LIBSA_EMBER_BOOT_LOGO_H */
