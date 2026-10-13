/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), malformed and maximum-width indexed operands. */
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <gelf.h>
#include <libdwarf.h>

#define CHECK(c) do { \
	if (!(c)) { \
		fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #c); \
		exit(1); \
	} \
} while (0)

int
main(int argc, char **argv)
{
	Dwarf_Debug dbg;
	Dwarf_Error error;
	Dwarf_Die root;
	Elf *elf;
	int fd, rc;

	CHECK(argc == 3);
	CHECK(elf_version(EV_CURRENT) != EV_NONE);
	CHECK((fd = open(argv[1], O_RDONLY)) >= 0);
	CHECK((elf = elf_begin(fd, ELF_C_READ, NULL)) != NULL);
	CHECK(dwarf_elf_init(elf, DW_DLC_READ, NULL, NULL, &dbg,
	    &error) == DW_DLV_OK);
	CHECK(dwarf_next_cu_header_d(dbg, 1, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL, &error) == DW_DLV_OK);
	rc = dwarf_siblingof_b(dbg, NULL, &root, 1, &error);
	if (atoi(argv[2]) == 3) {
		CHECK(rc == DW_DLV_OK);
		dwarf_dealloc(dbg, root, DW_DLA_DIE);
	} else {
		CHECK(rc == DW_DLV_ERROR);
		CHECK(dwarf_errno(error) == DW_DLE_ATTR_FORM_BAD);
	}
	CHECK(dwarf_finish(dbg, &error) == DW_DLV_OK);
	CHECK(elf_end(elf) == 0);
	CHECK(close(fd) == 0);
	return (0);
}
