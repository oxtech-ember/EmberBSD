/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), bounded DWARF package contribution views. */
#ifndef _CTF_DWARF_PACKAGE_H_
#define _CTF_DWARF_PACKAGE_H_

struct dw_package_view {
	Dwarf_Obj_Access_Interface iface;
	Dwarf_Obj_Access_Section sections[4];
	Dwarf_Small *data[4];
	Dwarf_Endianness endian;
	Dwarf_Small pointer;
};

static int
dw_package_section(void *arg, Dwarf_Half index,
    Dwarf_Obj_Access_Section *section, int *error __unused)
{
	struct dw_package_view *view = arg;
	if (index >= 4)
		return (DW_DLV_NO_ENTRY);
	*section = view->sections[index];
	return (DW_DLV_OK);
}

static Dwarf_Endianness
dw_package_endian(void *arg)
{
	return (((struct dw_package_view *)arg)->endian);
}

static Dwarf_Small
dw_package_length(void *arg __unused)
{
	return (4);
}

static Dwarf_Small
dw_package_pointer(void *arg)
{
	return (((struct dw_package_view *)arg)->pointer);
}

static Dwarf_Unsigned
dw_package_count(void *arg __unused)
{
	return (4);
}

static int
dw_package_load(void *arg, Dwarf_Half index, Dwarf_Small **data,
    int *error __unused)
{
	struct dw_package_view *view = arg;
	if (index >= 4)
		return (DW_DLV_NO_ENTRY);
	*data = view->data[index];
	return (DW_DLV_OK);
}

static const Dwarf_Obj_Access_Methods dw_package_methods = {
	dw_package_section, dw_package_endian, dw_package_length,
	dw_package_pointer, dw_package_count, dw_package_load
};

static uint32_t
dw_package_u32(const unsigned char *p, int little)
{
	if (little)
		return ((uint32_t)p[0] | (uint32_t)p[1] << 8 |
		    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
	return ((uint32_t)p[3] | (uint32_t)p[2] << 8 |
	    (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24);
}

/* Every referenced offset is relative to its indexed contribution. */
static void
dw_scan_package_index(dwarf_t *dw, Elf *elf, const char *name,
    const Dwarf_Sig8 *wanted)
{
	const char *sections[] = { ".debug_info.dwo", ".debug_abbrev.dwo",
	    ".debug_str_offsets.dwo", ".debug_str.dwo" };
	unsigned int identifiers[] = { 1, 3, 6 };
	Elf_Data *index = dw_section(elf, name), *data;
	struct dw_package_view *view;
	struct dw_context *context;
	const unsigned char *p, *hashes, *rows, *columns, *offsets, *sizes;
	unsigned char *seen;
	GElf_Ehdr eh;
	uint32_t n, u, slots, row, i, j, id, offset, version;
	Dwarf_Unsigned size;
	uint64_t cells, head, signature;
	uint32_t slot, step, probes;
	int little, found = 0;

	if (index == NULL) {
		if (wanted != NULL)
			terminate("missing DWARF package CU index\n");
		return;
	}
	if (index->d_size < 16 || gelf_getehdr(elf, &eh) == NULL)
		goto invalid;
	little = eh.e_ident[EI_DATA] == ELFDATA2LSB;
	p = index->d_buf;
	version = dw_package_u32(p, little);
	if (version != 5 && version != 2)
		terminate("unsupported DWARF package index version\n");
	if (version == 2 && wanted == NULL) {
		sections[0] = ".debug_types.dwo";
		identifiers[0] = 2;
	}
	n = dw_package_u32(p + 4, little);
	u = dw_package_u32(p + 8, little);
	slots = dw_package_u32(p + 12, little);
	if (u == 0 && n == 0 && slots == 0 && index->d_size == 16)
		return;
	if (n == 0 || n > 8 || slots == 0 || (slots & (slots - 1)) != 0 || u > slots)
		goto invalid;
	head = 16 + (uint64_t)slots * 12 + (uint64_t)n * 4;
	cells = (uint64_t)n * u;
	if (head > index->d_size || cells > (index->d_size - head) / 8 ||
	    head + cells * 8 != index->d_size)
		goto invalid;
	hashes = p + 16;
	rows = hashes + (uint64_t)slots * 8;
	columns = rows + (uint64_t)slots * 4;
	offsets = columns + (uint64_t)n * 4;
	sizes = offsets + cells * 4;
	for (i = 0; i < n; i++) {
		id = dw_package_u32(columns + i * 4, little);
		if (id < 1 || id > 8 || (version == 5 && id == 2))
			goto invalid;
		for (j = 0; j < i; j++)
			if (dw_package_u32(columns + j * 4, little) == id)
				goto invalid;
	}
	seen = xcalloc((size_t)u + 1);
	for (i = 0; i < slots; i++) {
		row = dw_package_u32(rows + (uint64_t)i * 4, little);
		if (row == 0) {
			if (memcmp(hashes + (uint64_t)i * 8, "\0\0\0\0\0\0\0\0", 8))
				goto invalid;
			continue;
		}
		if (row > u || seen[row])
			goto invalid;
		/* Enforce hash placement and reject duplicate signatures. */
		signature = little ?
		    (uint64_t)dw_package_u32(hashes + (uint64_t)i * 8 + 4, 1) << 32 |
		    dw_package_u32(hashes + (uint64_t)i * 8, 1) :
		    (uint64_t)dw_package_u32(hashes + (uint64_t)i * 8, 0) << 32 |
		    dw_package_u32(hashes + (uint64_t)i * 8 + 4, 0);
		slot = signature & (slots - 1);
		step = ((signature >> 32) & (slots - 1)) | 1;
		for (probes = 0; slot != i; probes++) {
			if (probes >= slots || dw_package_u32(rows + (uint64_t)slot * 4, little) == 0 ||
			    memcmp(hashes + (uint64_t)slot * 8, hashes + (uint64_t)i * 8, 8) == 0)
				goto invalid;
			slot = (slot + step) & (slots - 1);
		}
		seen[row] = 1;
		if (wanted != NULL && memcmp(hashes + (uint64_t)i * 8, wanted, 8))
			continue;
		if (wanted != NULL && found++)
			goto invalid;
		view = xcalloc(sizeof(*view));
		view->iface.object = view;
		view->iface.methods = &dw_package_methods;
		view->endian = little ? DW_OBJECT_LSB : DW_OBJECT_MSB;
		view->pointer = dw->dw_ptrsz;
		for (j = 0; j < 4; j++) {
			view->sections[j].name = sections[j];
			data = dw_section(elf, sections[j]);
			offset = 0;
			size = data == NULL ? 0 : data->d_size;
			if (j != 3) {
				uint32_t col;
				for (col = 0; col < n; col++)
					if (dw_package_u32(columns + col * 4, little) == identifiers[j])
						break;
				if (col == n) {
					if (j < 2)
						goto invalid;
					size = 0;
				} else {
					uint64_t cell = ((uint64_t)(row - 1) * n + col) * 4;
					offset = dw_package_u32(offsets + cell, little);
					size = dw_package_u32(sizes + cell, little);
				}
			}
			if ((data == NULL && (offset != 0 || size != 0)) ||
			    (data != NULL && (offset > data->d_size || size > data->d_size - offset)))
				goto invalid;
			view->sections[j].size = size;
			view->data[j] = data == NULL ? NULL : (unsigned char *)data->d_buf + offset;
		}
		if (view->sections[0].size == 0 || view->sections[1].size == 0 ||
		    view->sections[0].size > TID_FILEMAX - dw->dw_maxoff)
			goto invalid;
		context = xcalloc(sizeof(*context));
		context->package = view;
		context->is_info = version == 5 || wanted != NULL;
		context->little = little;
		context->split = 1;
		context->base = dw->dw_maxoff;
		context->size = view->sections[0].size;
		dw->dw_maxoff += context->size;
		if (dwarf_object_init(&view->iface, NULL, NULL, &context->dbg, &dw->dw_err) != DW_DLV_OK)
			terminate("cannot initialize DWARF package contribution\n");
		context->next = dw->dw_contexts;
		dw->dw_contexts = context;
		dw_scan_context(dw, context);
		if (dw->dw_units->context != context ||
		    (dw->dw_units->next != NULL && dw->dw_units->next->context == context) ||
		    dw->dw_units->type != (wanted == NULL ? DW_UT_split_type : DW_UT_split_compile) ||
		    memcmp(&dw->dw_units->signature, hashes + (uint64_t)i * 8, 8))
			terminate("DWARF package index signature or unit mismatch\n");
	}
	for (i = 1; i <= u; i++)
		if (!seen[i])
			goto invalid;
	free(seen);
	if (wanted != NULL && !found)
		terminate("split DWARF dwo_id missing from package\n");
	return;
invalid:
	terminate("invalid DWARF package index or contribution\n");
}
#endif
