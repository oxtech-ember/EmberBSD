/*-
 * Copyright (c) 2026 EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE.
 */

/* Original, AI-assisted host/target check of caller-supplied firmware. */
#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "../../sys/dev/ic/cixvpu_fw.h"

static void
print_field(const char *name, const uint8_t *field, size_t size)
{
	size_t i;

	printf("%s=", name);
	for (i = 0; i < size && field[i] != 0; i++) {
		if (field[i] >= 32 && field[i] <= 126 && field[i] != '\\')
			putchar(field[i]);
		else
			printf("\\x%02x", (unsigned int)field[i]);
	}
	putchar('\n');
}

int
main(int argc, char **argv)
{
	uint8_t header[CIXVPU_FW_HEADER_SIZE], buffer[4096];
	struct cixvpu_fw_info info;
	struct stat st;
	enum cixvpu_fw_error error;
	FILE *file;
	size_t count, chunk, total;
	int saved_errno;

	if (argc != 2) {
		fprintf(stderr, "usage: %s firmware.fwb\n", argv[0]);
		return 2;
	}
	file = fopen(argv[1], "rb");
	if (file == NULL) {
		fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	if (fstat(fileno(file), &st) == -1) {
		saved_errno = errno;
		(void)fclose(file);
		fprintf(stderr, "%s: %s\n", argv[1], strerror(saved_errno));
		return 1;
	}
	if (!S_ISREG(st.st_mode) || st.st_size < 0 ||
	    (uintmax_t)st.st_size > SIZE_MAX) {
		(void)fclose(file);
		fprintf(stderr, "%s: requires a regular, addressable file\n",
		    argv[1]);
		return 1;
	}
	count = fread(header, 1, sizeof(header), file);
	total = count;
	while ((chunk = fread(buffer, 1, sizeof(buffer), file)) != 0) {
		if (chunk > SIZE_MAX - total) {
			(void)fclose(file);
			fprintf(stderr, "%s: firmware file is too large\n", argv[1]);
			return 1;
		}
		total += chunk;
	}
	if (ferror(file)) {
		saved_errno = errno;
		(void)fclose(file);
		fprintf(stderr, "%s: %s\n", argv[1], strerror(saved_errno));
		return 1;
	}
	if (fclose(file) == EOF) {
		fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	if (total != (size_t)st.st_size) {
		fprintf(stderr, "%s: firmware file changed while reading\n", argv[1]);
		return 1;
	}
	/* The parser reads only the header and verifies the full text extent. */
	if (count != sizeof(header))
		error = CIXVPU_FW_SHORT;
	else
		error = cixvpu_fw_parse(header, total, &info);
	if (error != CIXVPU_FW_OK) {
		fprintf(stderr, "%s: %s\n", argv[1], cixvpu_fw_strerror(error));
		return 1;
	}
	printf("protocol=%u.%u\n", (unsigned int)info.major,
	    (unsigned int)info.minor);
	printf("format-bytes=%u,%u\n", (unsigned int)info.format_bytes[0],
	    (unsigned int)info.format_bytes[1]);
	print_field("description", info.description, sizeof(info.description));
	print_field("part", info.part, sizeof(info.part));
	print_field("revision", info.revision, sizeof(info.revision));
	print_field("version", info.version, sizeof(info.version));
	printf("jump=0x%08" PRIx32 "\n", info.jump);
	printf("text-bytes=%" PRIu32 "\ntext-pages=%" PRIu32 "\n",
	    info.text_size, info.text_pages);
	printf("bss-address=0x%08" PRIx32 "\nbss-pages=%" PRIu32 "\n",
	    info.bss_address, info.bss_pages);
	printf("shared-address=0x%08" PRIx32 "\nshared-bytes=%" PRIu32 "\n",
	    info.shared_address, info.shared_size);
	printf("shared-pages=%" PRIu32 "\nprivate-pages=%" PRIu32 "\n",
	    info.shared_pages, info.private_pages);
	return fflush(stdout) == EOF ? 1 : 0;
}
