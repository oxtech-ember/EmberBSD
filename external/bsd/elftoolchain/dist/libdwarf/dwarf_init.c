/*	$NetBSD: dwarf_init.c,v 1.5 2024/03/03 17:37:31 christos Exp $	*/

/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * Copyright (c) 2009 Kai Wang
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "_libdwarf.h"

__RCSID("$NetBSD: dwarf_init.c,v 1.5 2024/03/03 17:37:31 christos Exp $");
ELFTC_VCSID("Id: dwarf_init.c 2073 2011-10-27 03:30:47Z jkoshy");

/* Origin: EmberBSD (AI-assisted), explicit caller-owned supplementary data. */
int
dwarf_set_tied_dbg(Dwarf_Debug dbg, Dwarf_Debug tied, Dwarf_Error *error)
{
	if (dbg == NULL || dbg == tied || (tied != NULL &&
	    (tied->dbg_tied != NULL || (dbg->dbg_machine != 0 && tied->dbg_machine != 0 &&
	    dbg->dbg_machine != tied->dbg_machine) ||
	    dbg->dbg_pointer_size != tied->dbg_pointer_size))) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}
	dbg->dbg_tied = tied;
	return (DW_DLV_OK);
}

int
dwarf_elf_init_section(Elf *elf, int mode, Dwarf_Unsigned section,
    Dwarf_Handler errhand, Dwarf_Ptr errarg, Dwarf_Debug *ret_dbg,
    Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	int ret;
	Elf_Scn *scn;
	GElf_Shdr sh;
	size_t names;
	const char *name;

	if (elf == NULL || ret_dbg == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (mode != DW_DLC_READ) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	/* Origin: EmberBSD (AI-assisted), select one relocatable debug section. */
	if (section != 0 &&
	    ((scn = elf_getscn(elf, section)) == NULL ||
	    gelf_getshdr(scn, &sh) == NULL || !elf_getshstrndx(elf, &names) ||
	    (name = elf_strptr(elf, names, sh.sh_name)) == NULL ||
	    (strcmp(name, ".debug_info") && strcmp(name, ".debug_info.dwo") &&
	    strcmp(name, ".debug_types") && strcmp(name, ".debug_types.dwo")))) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (_dwarf_alloc(&dbg, mode, error) != DW_DLE_NONE)
		return (DW_DLV_ERROR);

	if (_dwarf_elf_init(dbg, elf, section, error) != DW_DLE_NONE) {
		free(dbg);
		return (DW_DLV_ERROR);
	}

	if ((ret = _dwarf_init(dbg, 0, errhand, errarg, error)) !=
	    DW_DLE_NONE) {
		_dwarf_elf_deinit(dbg);
		free(dbg);
		if (ret == DW_DLE_DEBUG_INFO_NULL)
			return (DW_DLV_NO_ENTRY);
		else
			return (DW_DLV_ERROR);
	}

	*ret_dbg = dbg;

	return (DW_DLV_OK);
}

/* Retain the all-sections entry point. */
int
dwarf_elf_init(Elf *elf, int mode, Dwarf_Handler errhand, Dwarf_Ptr errarg,
    Dwarf_Debug *ret_dbg, Dwarf_Error *error)
{
	return (dwarf_elf_init_section(elf, mode, 0, errhand, errarg,
	    ret_dbg, error));
}

int
dwarf_get_elf(Dwarf_Debug dbg, Elf **elf, Dwarf_Error *error)
{
	Dwarf_Elf_Object *e;

	if (dbg == NULL || elf == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	e = dbg->dbg_iface->object;
	*elf = e->eo_elf;

	return (DW_DLV_OK);
}

int
dwarf_init(int fd, int mode, Dwarf_Handler errhand, Dwarf_Ptr errarg,
    Dwarf_Debug *ret_dbg, Dwarf_Error *error)
{
	Dwarf_Debug dbg;
	Elf *elf;
	int ret;

	if (fd < 0 || ret_dbg == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (mode != DW_DLC_READ) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (elf_version(EV_CURRENT) == EV_NONE) {
		DWARF_SET_ELF_ERROR(NULL, error);
		return (DW_DLV_ERROR);
	}

	if ((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		DWARF_SET_ELF_ERROR(NULL, error);
		return (DW_DLV_ERROR);
	}

	if (_dwarf_alloc(&dbg, mode, error) != DW_DLE_NONE)
		return (DW_DLV_ERROR);

	if (_dwarf_elf_init(dbg, elf, 0, error) != DW_DLE_NONE) {
		free(dbg);
		return (DW_DLV_ERROR);
	}

	if ((ret = _dwarf_init(dbg, 0, errhand, errarg, error)) !=
	    DW_DLE_NONE) {
		_dwarf_elf_deinit(dbg);
		free(dbg);
		if (ret == DW_DLE_DEBUG_INFO_NULL)
			return (DW_DLV_NO_ENTRY);
		else
			return (DW_DLV_ERROR);
	}

	*ret_dbg = dbg;

	return (DW_DLV_OK);
}

int
dwarf_object_init(Dwarf_Obj_Access_Interface *iface, Dwarf_Handler errhand,
    Dwarf_Ptr errarg, Dwarf_Debug *ret_dbg, Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	if (iface == NULL || ret_dbg == NULL) {
		DWARF_SET_ERROR(NULL, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (_dwarf_alloc(&dbg, DW_DLC_READ, error) != DW_DLE_NONE)
		return (DW_DLV_ERROR);

	dbg->dbg_iface = iface;

	if (_dwarf_init(dbg, 0, errhand, errarg, error) != DW_DLE_NONE) {
		free(dbg);
		return (DW_DLV_ERROR);
	}

	*ret_dbg = dbg;

	return (DW_DLV_OK);
}
