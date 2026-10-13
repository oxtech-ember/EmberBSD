/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), libdwarf section selection boundary contract. */
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gelf.h>
#include <libdwarf.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

int
main(int argc, char **argv)
{
	Dwarf_Debug dbg, legacy;
	Dwarf_Error error;
	Dwarf_Die root, child, sibling;
	Dwarf_Unsigned length, old_length;
	Dwarf_Half type, old_type;
	Elf *elf;
	Elf_Scn *scn = NULL;
	GElf_Shdr sh;
	const char *name;
	size_t names;
	unsigned int sections = 0;
	int fd, info, rc;

	CHECK(argc == 2);
	CHECK(elf_version(EV_CURRENT) != EV_NONE);
	CHECK((fd = open(argv[1], O_RDONLY)) >= 0);
	CHECK((elf = elf_begin(fd, ELF_C_READ, NULL)) != NULL);
	CHECK(elf_getshstrndx(elf, &names));
	CHECK(dwarf_elf_init_section(elf, DW_DLC_READ, ~(Dwarf_Unsigned)0,
	    NULL, NULL, &dbg, &error) == DW_DLV_ERROR);
	CHECK(dwarf_errno(error) == DW_DLE_ARGUMENT);
	CHECK(dwarf_elf_init_section(elf, DW_DLC_READ, 0, NULL, NULL, &dbg, &error) == DW_DLV_OK);
	CHECK(dwarf_elf_init(elf, DW_DLC_READ, NULL, NULL, &legacy, &error) == DW_DLV_OK);
	CHECK(dwarf_next_cu_header_d(dbg, 1, &length, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, &type, &error) == DW_DLV_OK);
	CHECK(dwarf_next_cu_header_d(legacy, 1, &old_length, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, &old_type, &error) == DW_DLV_OK);
	CHECK(length == old_length && type == old_type);
	CHECK(dwarf_finish(dbg, &error) == DW_DLV_OK);
	CHECK(dwarf_finish(legacy, &error) == DW_DLV_OK);
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		CHECK(gelf_getshdr(scn, &sh) != NULL);
		CHECK((name = elf_strptr(elf, names, sh.sh_name)) != NULL);
		info = !strcmp(name, ".debug_info") || !strcmp(name, ".debug_info.dwo");
		if (!info && strcmp(name, ".debug_types") && strcmp(name, ".debug_types.dwo")) {
			CHECK(dwarf_elf_init_section(elf, DW_DLC_READ, elf_ndxscn(scn),
			    NULL, NULL, &dbg, &error) == DW_DLV_ERROR);
			CHECK(dwarf_errno(error) == DW_DLE_ARGUMENT);
			continue;
		}
		CHECK(dwarf_elf_init_section(elf, DW_DLC_READ, elf_ndxscn(scn),
		    NULL, NULL, &dbg, &error) == DW_DLV_OK);
		CHECK(dwarf_next_cu_header_d(dbg, info, NULL, NULL, NULL, NULL,
		    NULL, NULL, NULL, NULL, NULL, NULL, &error) == DW_DLV_OK);
		CHECK(dwarf_siblingof_b(dbg, NULL, &root, info, &error) == DW_DLV_OK);
		while ((rc = dwarf_next_cu_header_d(dbg, info, NULL, NULL, NULL, NULL,
		    NULL, NULL, NULL, NULL, NULL, NULL, &error)) == DW_DLV_OK)
			continue;
		CHECK(rc == DW_DLV_NO_ENTRY);
		CHECK(dwarf_child(root, &child, &error) == DW_DLV_OK);
		/* A DIE keeps its CU even after header iteration reaches its end. */
		rc = dwarf_siblingof_b(dbg, child, &sibling, info, &error);
		CHECK(rc == DW_DLV_OK || rc == DW_DLV_NO_ENTRY);
		CHECK(dwarf_finish(dbg, &error) == DW_DLV_OK);
		sections++;
	}
	CHECK(sections != 0);
	CHECK(elf_end(elf) == 0);
	CHECK(close(fd) == 0);
	printf("PASS: section API boundaries, legacy parity and retained DIE traversal (%u sections)\n", sections);
	return (0);
}
