/* Origin: EmberBSD ASCII boot identity, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _LIBSA_EMBER_BOOT_LOGO_H
#define _LIBSA_EMBER_BOOT_LOGO_H

/* Use printable ASCII on firmware, BIOS and serial text consoles. */
static inline void
ember_boot_logo(void)
{
	printf(
	    "         /\\\n"
	    "        /  \\      #####  #   #  ####   #####  ####   ####    ####  ####\n"
	    "     /\\/    \\     #      ## ##  #   #  #      #   #  #   #  #      #   #\n"
	    "    /        \\    #      # # #  #   #  #      #   #  #   #  #      #   #\n"
	    "   /   /\\     \\   ####   #   #  ####   ####   ####   ####    ###   #   #\n"
	    "   \\  /  \\    /   #      #   #  #   #  #      # #    #   #      #  #   #\n"
	    "    \\/    \\__/    #      #   #  #   #  #      #  #   #   #      #  #   #\n"
	    "     \\____/       #####  #   #  ####   #####  #   #  ####   ####   ####\n"
	    "\n");
}

#endif /* _LIBSA_EMBER_BOOT_LOGO_H */
