/*	$NetBSD: dwarf_form.c,v 1.5 2024/03/03 17:37:30 christos Exp $	*/

/*-
 * Copyright (c) 2007 John Birrell (jb@freebsd.org)
 * Copyright (c) 2009,2010,2023 Kai Wang
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

__RCSID("$NetBSD: dwarf_form.c,v 1.5 2024/03/03 17:37:30 christos Exp $");
ELFTC_VCSID("Id: dwarf_form.c 4016 2023-10-15 05:39:46Z kaiwang27");

int
dwarf_hasform(Dwarf_Attribute at, Dwarf_Half form, Dwarf_Bool *return_hasform,
    Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_hasform == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*return_hasform = (at->at_form == form);

	return (DW_DLV_OK);
}

int
dwarf_whatform(Dwarf_Attribute at, Dwarf_Half *return_form, Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_form == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*return_form = at->at_form;

	return (DW_DLV_OK);
}

int
dwarf_whatform_direct(Dwarf_Attribute at, Dwarf_Half *return_form,
    Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_form == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (at->at_indirect)
		*return_form = DW_FORM_indirect;
	else
		*return_form = (Dwarf_Half) at->at_form;

	return (DW_DLV_OK);
}

int
dwarf_whatattr(Dwarf_Attribute at, Dwarf_Half *return_attr, Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_attr == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	*return_attr = (Dwarf_Half) at->at_attrib;

	return (DW_DLV_OK);
}

int
dwarf_formref(Dwarf_Attribute at, Dwarf_Off *return_offset, Dwarf_Error *error)
{
	int ret;
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_offset == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	switch (at->at_form) {
	case DW_FORM_ref1:
	case DW_FORM_ref2:
	case DW_FORM_ref4:
	case DW_FORM_ref8:
	case DW_FORM_ref_udata:
		*return_offset = (Dwarf_Off) at->u[0].u64;
		ret = DW_DLV_OK;
		break;
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLV_ERROR;
	}

	return (ret);
}

int
dwarf_global_formref(Dwarf_Attribute at, Dwarf_Off *return_offset,
    Dwarf_Error *error)
{
	int ret;
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_offset == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	switch (at->at_form) {
	case DW_FORM_ref_addr:
	case DW_FORM_ref_sup4:
	case DW_FORM_ref_sup8:
	case DW_FORM_sec_offset:
		*return_offset = (Dwarf_Off) at->u[0].u64;
		ret = DW_DLV_OK;
		break;
	case DW_FORM_ref1:
	case DW_FORM_ref2:
	case DW_FORM_ref4:
	case DW_FORM_ref8:
	case DW_FORM_ref_udata:
		*return_offset = (Dwarf_Off) at->u[0].u64 +
			at->at_die->die_cu->cu_offset;
		ret = DW_DLV_OK;
		break;
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLV_ERROR;
	}

	return (ret);
}

int
dwarf_formaddr(Dwarf_Attribute at, Dwarf_Addr *return_addr, Dwarf_Error *error)
{
	int ret;
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_addr == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (at->at_form == DW_FORM_addr) {
		*return_addr = at->u[0].u64;
		ret = DW_DLV_OK;
	} else {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLV_ERROR;
	}

	return (ret);
}

int
dwarf_formflag(Dwarf_Attribute at, Dwarf_Bool *return_bool, Dwarf_Error *error)
{
	int ret;
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_bool == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (at->at_form == DW_FORM_flag ||
	    at->at_form == DW_FORM_flag_present) {
		*return_bool = (Dwarf_Bool) (!!at->u[0].u64);
		ret = DW_DLV_OK;
	} else {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLV_ERROR;
	}

	return (ret);
}

int
dwarf_formudata(Dwarf_Attribute at, Dwarf_Unsigned *return_uvalue,
    Dwarf_Error *error)
{
	int ret;
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_uvalue == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	switch (at->at_form) {
	case DW_FORM_data1:
	case DW_FORM_data2:
	case DW_FORM_data4:
	case DW_FORM_data8:
	case DW_FORM_udata:
	case DW_FORM_implicit_const:
		*return_uvalue = at->u[0].u64;
		ret = DW_DLV_OK;
		break;
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLV_ERROR;
	}

	return (ret);
}

int
dwarf_formsdata(Dwarf_Attribute at, Dwarf_Signed *return_svalue,
    Dwarf_Error *error)
{
	int ret;
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_svalue == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	switch (at->at_form) {
	case DW_FORM_data1:
		*return_svalue = (int8_t) at->u[0].s64;
		ret = DW_DLV_OK;
		break;
	case DW_FORM_data2:
		*return_svalue = (int16_t) at->u[0].s64;
		ret = DW_DLV_OK;
		break;
	case DW_FORM_data4:
		*return_svalue = (int32_t) at->u[0].s64;
		ret = DW_DLV_OK;
		break;
	case DW_FORM_data8:
	case DW_FORM_sdata:
	case DW_FORM_implicit_const:
		*return_svalue = at->u[0].s64;
		ret = DW_DLV_OK;
		break;
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLV_ERROR;
	}

	return (ret);
}

int
dwarf_formblock(Dwarf_Attribute at, Dwarf_Block **return_block,
    Dwarf_Error *error)
{
	int ret;
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_block == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	switch (at->at_form) {
	case DW_FORM_block:
	case DW_FORM_block1:
	case DW_FORM_block2:
	case DW_FORM_block4:
		*return_block = &at->at_block;
		ret = DW_DLV_OK;
		break;
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLV_ERROR;
	}

	return (ret);
}

int
dwarf_formsig8(Dwarf_Attribute at, Dwarf_Sig8 *return_sig8, Dwarf_Error *error)
{
	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_sig8 == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}
	
	if (at->at_form != DW_FORM_ref_sig8) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		return (DW_DLV_ERROR);
	}

	assert(at->u[0].u64 == 8);
	memcpy(return_sig8->signature, at->u[1].u8p, at->u[0].u64);

	return (DW_DLV_OK);
}

int
dwarf_formexprloc(Dwarf_Attribute at, Dwarf_Unsigned *return_exprlen,
    Dwarf_Ptr *return_expr, Dwarf_Error *error)
{

	Dwarf_Debug dbg;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_exprlen == NULL || return_expr == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	if (at->at_form != DW_FORM_exprloc) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		return (DW_DLV_ERROR);
	}

	*return_exprlen = at->u[0].u64;
	*return_expr = (void *) at->u[1].u8p;

	return (DW_DLV_OK);
}

/* Origin: EmberBSD (AI-assisted), resolve DWARF5 indexed strings safely. */
static int
_dwarf_form_indexed_string(Dwarf_Attribute at, char **strp, Dwarf_Error *error)
{
	Dwarf_Debug dbg = at->at_die->die_dbg;
	Dwarf_CU cu = at->at_die->die_cu;
	Dwarf_Die root;
	Dwarf_Attribute baseattr;
	Dwarf_Section *offsets, *strings;
	uint64_t base, index, offset, length, limit;
	unsigned int width = cu->cu_dwarf_size;
	int ret;

	/* GNU DWARF4 uses a headerless, contribution-relative offset table. */
	if (at->at_form == DW_FORM_GNU_str_index) {
		offsets = _dwarf_find_section(dbg, ".debug_str_offsets");
		strings = _dwarf_find_section(dbg, ".debug_str");
		index = at->u[0].u64;
		if (cu->cu_version != 4 || offsets == NULL || strings == NULL ||
		    (width != 4 && width != 8) || offsets->ds_size % width != 0 ||
		    index >= offsets->ds_size / width)
			goto invalid;
		offset = index * width;
		goto read_string;
	}

	/* Resolve after the root's attributes have all been parsed. */
	ret = dwarf_offdie_b(dbg, cu->cu_1st_offset, cu->cu_is_info,
	    &root, error);
	if (ret != DW_DLV_OK)
		return (ret);
	baseattr = _dwarf_attr_find(root, DW_AT_str_offsets_base);
	if (baseattr == NULL && (cu->cu_unit_type == DW_UT_split_compile ||
	    cu->cu_unit_type == DW_UT_split_type)) {
		/* DWARF5 7.26: a standalone .dwo has an implicit first base. */
		base = width == 4 ? 8 : 16;
	} else if (baseattr == NULL || baseattr->at_form != DW_FORM_sec_offset) {
		dwarf_dealloc(dbg, root, DW_DLA_DIE);
		goto invalid;
	} else
		base = baseattr->u[0].u64;
	dwarf_dealloc(dbg, root, DW_DLA_DIE);
	offsets = _dwarf_find_section(dbg, ".debug_str_offsets");
	strings = _dwarf_find_section(dbg, ".debug_str");
	index = at->u[0].u64;
	if (offsets == NULL || strings == NULL ||
	    (width != 4 && width != 8) || base > offsets->ds_size ||
	    base < (width == 4 ? 8U : 16U))
		goto invalid;
	/* DWARF5 7.26: base points just past this contribution's header. */
	offset = base - (width == 4 ? 8 : 16);
	length = dbg->read(offsets->ds_data, &offset, 4);
	if (width == 8) {
		if (length != 0xffffffff)
			goto invalid;
		length = dbg->read(offsets->ds_data, &offset, 8);
	} else if (length >= 0xfffffff0) {
		goto invalid;
	}
	if (length < 4 || length > offsets->ds_size - offset ||
	    (length - 4) % width != 0)
		goto invalid;
	limit = offset + length;
	if (dbg->read(offsets->ds_data, &offset, 2) != 5 ||
	    dbg->read(offsets->ds_data, &offset, 2) != 0 ||
	    index >= (limit - base) / width)
		goto invalid;
	offset = base + index * width;
read_string:
	offset = dbg->read(offsets->ds_data, &offset, width);
	if (offset >= strings->ds_size || memchr(strings->ds_data + offset,
	    '\0', strings->ds_size - offset) == NULL)
		goto invalid;
	*strp = (char *)strings->ds_data + offset;
	return (DW_DLV_OK);
invalid:
	DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
	return (DW_DLV_ERROR);
}

int
dwarf_formstring(Dwarf_Attribute at, char **return_string,
    Dwarf_Error *error)
{
	int ret;
	Dwarf_Debug dbg;
	Dwarf_Section *strings;

	dbg = at != NULL ? at->at_die->die_dbg : NULL;

	if (at == NULL || return_string == NULL) {
		DWARF_SET_ERROR(dbg, error, DW_DLE_ARGUMENT);
		return (DW_DLV_ERROR);
	}

	switch (at->at_form) {
	case DW_FORM_strp_sup:
		/* Resolve lazily, after the consumer has attached its supplement. */
		strings = dbg->dbg_tied == NULL ? NULL :
		    _dwarf_find_section(dbg->dbg_tied, ".debug_str");
		if (strings == NULL || at->u[0].u64 >= strings->ds_size ||
		    memchr(strings->ds_data + at->u[0].u64, 0,
		    strings->ds_size - at->u[0].u64) == NULL) {
			DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
			return (DW_DLV_ERROR);
		}
		*return_string = (char *)strings->ds_data + at->u[0].u64;
		return (DW_DLV_OK);
	case DW_FORM_string:
		*return_string = (char *) at->u[0].s;
		ret = DW_DLV_OK;
		break;
	case DW_FORM_strp:
	case DW_FORM_line_strp:
		*return_string = (char *) at->u[1].s;
		ret = DW_DLV_OK;
		break;
	case DW_FORM_GNU_str_index:
	case DW_FORM_strx:
	case DW_FORM_strx1:
	case DW_FORM_strx2:
	case DW_FORM_strx3:
	case DW_FORM_strx4:
		return (_dwarf_form_indexed_string(at, return_string, error));
	default:
		DWARF_SET_ERROR(dbg, error, DW_DLE_ATTR_FORM_BAD);
		ret = DW_DLV_ERROR;
	}

	return (ret);
}

enum Dwarf_Form_Class
dwarf_get_form_class(Dwarf_Half dwversion, Dwarf_Half attr,
    Dwarf_Half offset_size, Dwarf_Half form)
{

	switch (form) {
	case DW_FORM_addr:
		return (DW_FORM_CLASS_ADDRESS);
	case DW_FORM_block:
	case DW_FORM_block1:
	case DW_FORM_block2:
	case DW_FORM_block4:
		return (DW_FORM_CLASS_BLOCK);
	case DW_FORM_string:
	case DW_FORM_strp:
		return (DW_FORM_CLASS_STRING);
	case DW_FORM_flag:
	case DW_FORM_flag_present:
		return (DW_FORM_CLASS_FLAG);
	case DW_FORM_ref_addr:
	case DW_FORM_ref_sig8:
	case DW_FORM_ref_udata:
	case DW_FORM_ref1:
	case DW_FORM_ref2:
	case DW_FORM_ref4:
	case DW_FORM_ref8:
		return (DW_FORM_CLASS_REFERENCE);
	case DW_FORM_exprloc:
		return (DW_FORM_CLASS_EXPRLOC);
	case DW_FORM_data1:
	case DW_FORM_data2:
	case DW_FORM_sdata:
	case DW_FORM_udata:
		return (DW_FORM_CLASS_CONSTANT);
	case DW_FORM_data4:
	case DW_FORM_data8:
		if (dwversion > 3)
			return (DW_FORM_CLASS_CONSTANT);
		if (form == DW_FORM_data4 && offset_size != 4)
			return (DW_FORM_CLASS_CONSTANT);
		if (form == DW_FORM_data8 && offset_size != 8)
			return (DW_FORM_CLASS_CONSTANT);
		/* FALLTHROUGH */
	case DW_FORM_sec_offset:
		/*
		 * DW_FORM_data4 and DW_FORM_data8 can be used as
		 * offset/pointer before DWARF4. Newly added
		 * DWARF4 form DW_FORM_sec_offset intents to replace
		 * DW_FORM_data{4,8} for this purpose. Anyway, to
		 * determine the actual class for these forms, we need
		 * to also look at the attribute number.
		 */
		switch (attr) {
		case DW_AT_location:
		case DW_AT_string_length:
		case DW_AT_return_addr:
		case DW_AT_data_member_location:
		case DW_AT_frame_base:
		case DW_AT_segment:
		case DW_AT_static_link:
		case DW_AT_use_location:
		case DW_AT_vtable_elem_location:
			return (DW_FORM_CLASS_LOCLISTPTR);
		case DW_AT_stmt_list:
			return (DW_FORM_CLASS_LINEPTR);
		case DW_AT_start_scope:
		case DW_AT_ranges:
			return (DW_FORM_CLASS_RANGELISTPTR);
		case DW_AT_macro_info:
			return (DW_FORM_CLASS_MACPTR);
		default:
			if (form == DW_FORM_data4 || form == DW_FORM_data8)
				return (DW_FORM_CLASS_CONSTANT);
			else
				return (DW_FORM_CLASS_UNKNOWN);
		}
	default:
		return (DW_FORM_CLASS_UNKNOWN);
	}
}
