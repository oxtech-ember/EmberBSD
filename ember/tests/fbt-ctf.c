/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), shared FBT decoder regression for CTF2/3. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "../../external/cddl/osnet/dist/uts/common/sys/ctf.h"
#include "../../external/cddl/osnet/dev/fbt/fbt_ctf.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void
records(void)
{
	/* Same named function-pointer type with three arguments, in both ABIs. */
	const uint16_t v2[] = { 1, 0, CTF_V2_TYPE_INFO(CTF_K_FUNCTION, 1, 3),
	    7, 1, 2, 3, 0 };
	const uint32_t v3[] = { 1, CTF_V3_TYPE_INFO(CTF_K_FUNCTION, 1, 3),
	    70000, 1, 2, 70001 };
	struct fbt_ctf_type t;
	size_t i;

	CHECK(fbt_ctf_decode(2, (const uint8_t *)v2, sizeof(v2), &t) == 0);
	CHECK(t.kind == CTF_K_FUNCTION && t.vlen == 3 && t.ref == 7);
	CHECK(t.hdrlen == 8 && t.reclen == 16);
	CHECK(fbt_ctf_decode(3, (const uint8_t *)v3, sizeof(v3), &t) == 0);
	CHECK(t.kind == CTF_K_FUNCTION && t.vlen == 3 && t.ref == 70000);
	CHECK(t.hdrlen == 12 && t.reclen == 24);
	/* The former FBT cast sees kind=0 for this valid CTF3 function. */
	CHECK(CTF_V2_INFO_KIND(fbt_ctf_word((const uint8_t *)v3 + 4, 2)) != CTF_K_FUNCTION);
	for (i = 0; i < sizeof(v2); i++)
		CHECK(fbt_ctf_decode(2, (const uint8_t *)v2, i, &t) != 0);
	for (i = 0; i < sizeof(v3); i++)
		CHECK(fbt_ctf_decode(3, (const uint8_t *)v3, i, &t) != 0);
	CHECK(fbt_ctf_decode(4, (const uint8_t *)v3, sizeof(v3), &t) != 0);
}

static void
kernel(const char *path)
{
	FILE *file = fopen(path, "rb");
	ctf_header_t h;
	struct fbt_ctf_type t;
	uint8_t *input, *data;
	long length;
	uLongf size;
	size_t off, count = 0;
	int lwp = 0, proc = 0;
	const char *name;

	CHECK(file != NULL);
	CHECK(fread(&h, sizeof(h), 1, file) == 1 && h.cth_magic == CTF_MAGIC);
	CHECK(fseek(file, 0, SEEK_END) == 0);
	length = ftell(file) - sizeof(h);
	CHECK(length > 0 && fseek(file, sizeof(h), SEEK_SET) == 0);
	input = malloc(length);
	CHECK(input != NULL && fread(input, length, 1, file) == 1);
	fclose(file);
	size = (uLongf)h.cth_stroff + h.cth_strlen;
	data = malloc(size);
	CHECK(data != NULL);
	if (h.cth_flags & CTF_F_COMPRESS)
		CHECK(uncompress(data, &size, input, length) == Z_OK);
	else {
		CHECK((uLongf)length == size);
		memcpy(data, input, size);
	}
	CHECK(size == (uLongf)h.cth_stroff + h.cth_strlen);
	for (off = h.cth_typeoff; off < h.cth_stroff; off += t.reclen) {
		CHECK(fbt_ctf_decode(h.cth_version, data + off, h.cth_stroff - off, &t) == 0);
		CHECK(t.name < h.cth_strlen);
		name = (const char *)data + h.cth_stroff + t.name;
		CHECK(memchr(name, 0, h.cth_strlen - t.name) != NULL);
		lwp |= t.kind == CTF_K_STRUCT && strcmp(name, "lwp") == 0;
		proc |= t.kind == CTF_K_STRUCT && strcmp(name, "proc") == 0;
		count++;
	}
	CHECK(off == h.cth_stroff && lwp && proc);
	printf("PASS: kernel CTF%u: %zu types, including lwp/proc\n", h.cth_version, count);
	free(data);
	free(input);
}

int
main(int argc, char **argv)
{
	records();
	puts("PASS: CTF2/3 function widths, large type IDs and all truncated record lengths");
	if (argc == 2)
		kernel(argv[1]);
	return 0;
}
