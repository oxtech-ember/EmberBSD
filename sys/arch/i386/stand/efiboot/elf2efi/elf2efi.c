/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 The EmberBSD Project contributors.
 * Developed with AI assistance.
 *
 * Convert the position independent ELF image of the x86 EFI boot loaders
 * into a PE32+ (x86_64) or PE32 (i386) EFI application.
 *
 * ld.lld links the loader with ldscript.lld at address 0. Every allocated
 * section that holds program code or data becomes one PE section at the
 * same relative virtual address, so the ELF link addresses are the PE
 * RVAs and ImageBase is 0. The dynamic relocations must all be
 * R_X86_64_RELATIVE or R_386_RELATIVE; they become the base relocations
 * of a generated .reloc section, which the firmware applies when it loads
 * the image. Any other relocation, section layout or input stops the
 * conversion: a loader that would need fixups the firmware cannot do must
 * not be produced.
 */

#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ELF, as far as this conversion needs it. */
#define	ELF_IDENT_SIZE		16
#define	ELFCLASS32		1
#define	ELFCLASS64		2
#define	ELFDATA2LSB		1
#define	EV_CURRENT		1
#define	ET_DYN			3
#define	EM_386			3
#define	EM_X86_64		62

#define	SHT_NULL		0
#define	SHT_PROGBITS		1
#define	SHT_SYMTAB		2
#define	SHT_STRTAB		3
#define	SHT_RELA		4
#define	SHT_HASH		5
#define	SHT_DYNAMIC		6
#define	SHT_NOTE		7
#define	SHT_NOBITS		8
#define	SHT_REL			9
#define	SHT_DYNSYM		11
#define	SHT_INIT_ARRAY		14
#define	SHT_FINI_ARRAY		15
#define	SHT_PREINIT_ARRAY	16
#define	SHT_GNU_HASH		0x6ffffff6U
#define	SHT_GNU_VERDEF		0x6ffffffdU
#define	SHT_GNU_VERNEED		0x6ffffffeU
#define	SHT_GNU_VERSYM		0x6fffffffU

#define	SHF_WRITE		0x1U
#define	SHF_ALLOC		0x2U
#define	SHF_EXECINSTR		0x4U

/* R_386_RELATIVE and R_X86_64_RELATIVE have the same value. */
#define	R_X86_RELATIVE		8U

/* PE/COFF. */
#define	PE_FILE_ALIGNMENT	0x200U
#define	PE_SECTION_ALIGNMENT	0x1000U
#define	PE_MAX_SECTIONS		16U
#define	PE_DOS_HEADER_SIZE	0x40U
#define	PE_SIGNATURE_SIZE	4U
#define	PE_COFF_HEADER_SIZE	20U
#define	PE32_OPTIONAL_SIZE	224U
#define	PE32PLUS_OPTIONAL_SIZE	240U
#define	PE_SECTION_HEADER_SIZE	40U
#define	PE_NAME_SIZE		8U

#define	IMAGE_FILE_MACHINE_I386		0x014cU
#define	IMAGE_FILE_MACHINE_AMD64	0x8664U
#define	IMAGE_FILE_EXECUTABLE_IMAGE	0x0002U
#define	IMAGE_FILE_LINE_NUMS_STRIPPED	0x0004U
#define	IMAGE_FILE_LOCAL_SYMS_STRIPPED	0x0008U
#define	IMAGE_FILE_LARGE_ADDRESS_AWARE	0x0020U
#define	IMAGE_FILE_32BIT_MACHINE	0x0100U
#define	IMAGE_FILE_DEBUG_STRIPPED	0x0200U
#define	IMAGE_NT_OPTIONAL_HDR32_MAGIC	0x010bU
#define	IMAGE_NT_OPTIONAL_HDR64_MAGIC	0x020bU
#define	IMAGE_SUBSYSTEM_EFI_APPLICATION	10U
#define	IMAGE_DIRECTORY_ENTRY_BASERELOC	5U
#define	IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16U
#define	IMAGE_REL_BASED_ABSOLUTE	0U
#define	IMAGE_REL_BASED_HIGHLOW		3U
#define	IMAGE_REL_BASED_DIR64		10U

#define	IMAGE_SCN_CNT_CODE		0x00000020U
#define	IMAGE_SCN_CNT_INITIALIZED_DATA	0x00000040U
#define	IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080U
#define	IMAGE_SCN_MEM_DISCARDABLE	0x02000000U
#define	IMAGE_SCN_MEM_EXECUTE		0x20000000U
#define	IMAGE_SCN_MEM_READ		0x40000000U
#define	IMAGE_SCN_MEM_WRITE		0x80000000U

struct elf_image {
	const uint8_t	*data;
	size_t		 size;
	int		 is64;
	uint64_t	 entry;
	uint64_t	 shoff;
	unsigned	 shentsize;
	unsigned	 shnum;
	unsigned	 shstrndx;
};

struct elf_section {
	uint32_t	 name;
	uint32_t	 type;
	uint64_t	 flags;
	uint64_t	 addr;
	uint64_t	 offset;
	uint64_t	 size;
	uint64_t	 addralign;
	uint64_t	 entsize;
};

struct pe_section {
	char		 name[PE_NAME_SIZE + 1];
	uint32_t	 rva;
	uint32_t	 vsize;
	uint32_t	 initsize;	/* bytes backed by data, 0 for .bss */
	uint32_t	 rawsize;
	uint32_t	 fileoff;
	uint32_t	 characteristics;
	uint8_t		*data;
};

struct pe_image {
	int		 is64;
	uint32_t	 entry;
	unsigned	 nsections;
	struct pe_section sections[PE_MAX_SECTIONS - 1];
	struct pe_section reloc;	/* generated, placed after sections */
	uint32_t	*relocs;
	size_t		 nrelocs;
	size_t		 relocs_alloc;
};

static const char *output_path;

static void __attribute__((__noreturn__, __format__(__printf__, 1, 2)))
fail(const char *fmt, ...)
{
	va_list ap;

	fputs("x86-elf2efi: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	if (output_path != NULL)
		(void)unlink(output_path);
	exit(1);
}

static void *
xcalloc(size_t count, size_t size)
{
	void *p;

	if (count == 0 || size == 0)
		count = size = 1;
	p = calloc(count, size);
	if (p == NULL)
		fail("out of memory");
	return p;
}

static uint16_t
get16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t
get32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
get64(const uint8_t *p)
{
	return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

static void
put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
put32(uint8_t *p, uint32_t v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

static void
put64(uint8_t *p, uint64_t v)
{
	put32(p, (uint32_t)v);
	put32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t
align_up(uint64_t value, uint64_t alignment)
{
	if (value > UINT64_MAX - (alignment - 1))
		fail("address %#llx overflows", (unsigned long long)value);
	return (value + alignment - 1) & ~(alignment - 1);
}

static uint32_t
to_u32(uint64_t value, const char *what)
{
	if (value > UINT32_MAX)
		fail("%s %#llx does not fit a PE image", what,
		    (unsigned long long)value);
	return (uint32_t)value;
}

/* Return LEN bytes at OFFSET of the ELF file, or stop if they are not all there. */
static const uint8_t *
elf_bytes(const struct elf_image *elf, uint64_t offset, uint64_t len,
    const char *what)
{
	if (offset > elf->size || len > elf->size - offset)
		fail("truncated ELF file: %s is outside the file", what);
	return elf->data + offset;
}

static void
elf_open(struct elf_image *elf, const uint8_t *data, size_t size)
{
	const uint8_t *h;
	unsigned type, machine, version;

	memset(elf, 0, sizeof(*elf));
	elf->data = data;
	elf->size = size;
	h = elf_bytes(elf, 0, ELF_IDENT_SIZE, "ELF identification");
	if (memcmp(h, "\177ELF", 4) != 0)
		fail("input is not an ELF file");
	if (h[4] != ELFCLASS32 && h[4] != ELFCLASS64)
		fail("unsupported ELF class %u", h[4]);
	if (h[5] != ELFDATA2LSB)
		fail("input is not a little-endian ELF file");
	if (h[6] != EV_CURRENT)
		fail("unsupported ELF version %u", h[6]);
	elf->is64 = h[4] == ELFCLASS64;
	h = elf_bytes(elf, 0, elf->is64 ? 64 : 52, "ELF header");

	type = get16(h + 16);
	machine = get16(h + 18);
	version = get32(h + 20);
	if (version != EV_CURRENT)
		fail("unsupported ELF version %u", version);
	if (type != ET_DYN)
		fail("ELF type %u is not ET_DYN; link the loader with -pie", type);
	if (elf->is64 ? machine != EM_X86_64 : machine != EM_386)
		fail("ELF machine %u is not %s", machine,
		    elf->is64 ? "EM_X86_64" : "EM_386");
	if (elf->is64) {
		elf->entry = get64(h + 24);
		elf->shoff = get64(h + 40);
		elf->shentsize = get16(h + 58);
		elf->shnum = get16(h + 60);
		elf->shstrndx = get16(h + 62);
	} else {
		elf->entry = get32(h + 24);
		elf->shoff = get32(h + 32);
		elf->shentsize = get16(h + 46);
		elf->shnum = get16(h + 48);
		elf->shstrndx = get16(h + 50);
	}
	if (elf->shentsize != (elf->is64 ? 64U : 40U))
		fail("unexpected ELF section header size %u", elf->shentsize);
	/* Extended section numbering uses 0 here; the loaders never need it. */
	if (elf->shnum == 0 || elf->shstrndx == 0 ||
	    elf->shstrndx >= elf->shnum)
		fail("ELF file has no usable section name table");
	(void)elf_bytes(elf, elf->shoff,
	    (uint64_t)elf->shnum * elf->shentsize, "section header table");
}

static void
elf_section(const struct elf_image *elf, unsigned index,
    struct elf_section *s)
{
	const uint8_t *p;

	p = elf->data + elf->shoff + (uint64_t)index * elf->shentsize;
	s->name = get32(p);
	s->type = get32(p + 4);
	if (elf->is64) {
		s->flags = get64(p + 8);
		s->addr = get64(p + 16);
		s->offset = get64(p + 24);
		s->size = get64(p + 32);
		s->addralign = get64(p + 48);
		s->entsize = get64(p + 56);
	} else {
		s->flags = get32(p + 8);
		s->addr = get32(p + 12);
		s->offset = get32(p + 16);
		s->size = get32(p + 20);
		s->addralign = get32(p + 32);
		s->entsize = get32(p + 36);
	}
}

static const char *
elf_section_name(const struct elf_image *elf, const struct elf_section *s)
{
	struct elf_section names;
	const uint8_t *table;
	uint64_t i;

	elf_section(elf, elf->shstrndx, &names);
	if (names.type != SHT_STRTAB)
		fail("ELF section name table is not a string table");
	table = elf_bytes(elf, names.offset, names.size, "section name table");
	for (i = s->name; i < names.size; i++) {
		if (table[i] == '\0')
			return (const char *)table + s->name;
	}
	fail("ELF section name at %u is not terminated", s->name);
}

/* Sections the dynamic linker would read; the firmware never needs them. */
static int
is_dynamic_link_section(uint32_t type)
{
	switch (type) {
	case SHT_STRTAB:
	case SHT_HASH:
	case SHT_DYNAMIC:
	case SHT_NOTE:
	case SHT_DYNSYM:
	case SHT_GNU_HASH:
	case SHT_GNU_VERDEF:
	case SHT_GNU_VERNEED:
	case SHT_GNU_VERSYM:
		return 1;
	default:
		return 0;
	}
}

static void
map_section(struct pe_image *pe, const struct elf_image *elf,
    const struct elf_section *s, const char *name)
{
	struct pe_section *out;

	if (strlen(name) > PE_NAME_SIZE)
		fail("section %s has a name longer than %u bytes", name,
		    PE_NAME_SIZE);
	if (strcmp(name, ".reloc") == 0)
		fail("input already has a .reloc section");
	if (s->addralign > PE_SECTION_ALIGNMENT)
		fail("section %s needs alignment %#llx above %#x", name,
		    (unsigned long long)s->addralign, PE_SECTION_ALIGNMENT);
	if (s->addr % PE_SECTION_ALIGNMENT != 0)
		fail("section %s at %#llx is not aligned to %#x", name,
		    (unsigned long long)s->addr, PE_SECTION_ALIGNMENT);
	if (pe->nsections >= PE_MAX_SECTIONS - 1)
		fail("too many sections");

	out = &pe->sections[pe->nsections++];
	memcpy(out->name, name, strlen(name) + 1);
	out->rva = to_u32(s->addr, "section address");
	out->vsize = to_u32(s->size, "section size");
	(void)to_u32((uint64_t)out->rva + out->vsize, "section end");
	if (s->flags & SHF_EXECINSTR) {
		out->characteristics = IMAGE_SCN_CNT_CODE |
		    IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
	} else if (s->type == SHT_NOBITS) {
		out->characteristics = IMAGE_SCN_CNT_UNINITIALIZED_DATA |
		    IMAGE_SCN_MEM_READ;
	} else {
		out->characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA |
		    IMAGE_SCN_MEM_READ;
	}
	if (s->flags & SHF_WRITE)
		out->characteristics |= IMAGE_SCN_MEM_WRITE;
	if (s->type != SHT_NOBITS) {
		out->initsize = out->vsize;
		out->data = xcalloc(out->vsize, 1);
		memcpy(out->data, elf_bytes(elf, s->offset, s->size, name),
		    out->vsize);
	}
}

static int
compare_sections(const void *a, const void *b)
{
	const struct pe_section *sa = a, *sb = b;

	return sa->rva < sb->rva ? -1 : sa->rva > sb->rva;
}

static int
compare_u32(const void *a, const void *b)
{
	uint32_t ua = *(const uint32_t *)a, ub = *(const uint32_t *)b;

	return ua < ub ? -1 : ua > ub;
}

static struct pe_section *
section_at(struct pe_image *pe, uint64_t rva, unsigned len)
{
	unsigned i;

	for (i = 0; i < pe->nsections; i++) {
		struct pe_section *s = &pe->sections[i];

		if (rva >= s->rva && rva - s->rva <= s->initsize &&
		    len <= s->initsize - (rva - s->rva))
			return s;
	}
	return NULL;
}

static void
add_relocation(struct pe_image *pe, uint32_t rva)
{
	if (pe->nrelocs == pe->relocs_alloc) {
		size_t n = pe->relocs_alloc ? pe->relocs_alloc * 2 : 64;
		uint32_t *p;

		if (n > SIZE_MAX / sizeof(*p))
			fail("out of memory");
		p = realloc(pe->relocs, n * sizeof(*p));
		if (p == NULL)
			fail("out of memory");
		pe->relocs = p;
		pe->relocs_alloc = n;
	}
	pe->relocs[pe->nrelocs++] = rva;
}

static void
translate_relocations(struct pe_image *pe, const struct elf_image *elf,
    const struct elf_section *s, const char *name)
{
	const uint8_t *table;
	unsigned word = elf->is64 ? 8 : 4;
	int rela = s->type == SHT_RELA;
	uint64_t entsize, i;

	if (!(s->flags & SHF_ALLOC))
		fail("static relocation section %s is not supported; "
		    "do not link with --emit-relocs", name);
	entsize = elf->is64 ? (rela ? 24 : 16) : (rela ? 12 : 8);
	if (s->entsize != entsize || s->size % entsize != 0)
		fail("relocation section %s has entry size %llu, expected %llu",
		    name, (unsigned long long)s->entsize,
		    (unsigned long long)entsize);
	table = elf_bytes(elf, s->offset, s->size, name);
	for (i = 0; i < s->size; i += entsize) {
		const uint8_t *r = table + i;
		uint64_t offset, info, addend = 0;
		uint32_t type, symbol;
		struct pe_section *target;

		if (elf->is64) {
			offset = get64(r);
			info = get64(r + 8);
			type = (uint32_t)info;
			symbol = (uint32_t)(info >> 32);
			if (rela)
				addend = get64(r + 16);
		} else {
			offset = get32(r);
			info = get32(r + 4);
			type = (uint32_t)(info & 0xff);
			symbol = (uint32_t)(info >> 8);
			if (rela)
				addend = get32(r + 8);
		}
		if (type != R_X86_RELATIVE || symbol != 0)
			fail("relocation type %u (symbol %u) at %#llx in %s is "
			    "not supported; only %s converts to a base "
			    "relocation", type, symbol,
			    (unsigned long long)offset, name,
			    elf->is64 ? "R_X86_64_RELATIVE" : "R_386_RELATIVE");
		target = section_at(pe, offset, word);
		if (target == NULL)
			fail("relocation at %#llx in %s is outside the "
			    "initialized image data", (unsigned long long)offset,
			    name);
		/*
		 * The image is linked at 0, so the value the firmware adjusts
		 * is the addend itself. ld.lld keeps RELA addends out of the
		 * section data; REL addends are already in place.
		 */
		if (rela) {
			uint8_t *p = target->data + (offset - target->rva);

			if (elf->is64)
				put64(p, addend);
			else
				put32(p, (uint32_t)addend);
		}
		add_relocation(pe, (uint32_t)offset);
	}
}

static void
read_elf(struct pe_image *pe, const struct elf_image *elf)
{
	struct elf_section s;
	unsigned i;
	int has_code = 0;

	memset(pe, 0, sizeof(*pe));
	pe->is64 = elf->is64;
	for (i = 1; i < elf->shnum; i++) {
		const char *name;

		elf_section(elf, i, &s);
		name = elf_section_name(elf, &s);
		if (!(s.flags & SHF_ALLOC) || s.size == 0 ||
		    s.type == SHT_REL || s.type == SHT_RELA ||
		    is_dynamic_link_section(s.type))
			continue;
		switch (s.type) {
		case SHT_PROGBITS:
		case SHT_NOBITS:
		case SHT_INIT_ARRAY:
		case SHT_FINI_ARRAY:
		case SHT_PREINIT_ARRAY:
			map_section(pe, elf, &s, name);
			break;
		default:
			fail("allocated section %s has unsupported type %#x",
			    name, s.type);
		}
	}
	if (pe->nsections == 0)
		fail("input has no allocated sections");
	qsort(pe->sections, pe->nsections, sizeof(pe->sections[0]),
	    compare_sections);
	for (i = 1; i < pe->nsections; i++) {
		const struct pe_section *prev = &pe->sections[i - 1];

		if (pe->sections[i].rva !=
		    align_up((uint64_t)prev->rva + prev->vsize,
		    PE_SECTION_ALIGNMENT))
			fail("section %s does not follow %s at the next %#x "
			    "boundary", pe->sections[i].name, prev->name,
			    PE_SECTION_ALIGNMENT);
	}
	for (i = 0; i < pe->nsections; i++) {
		const struct pe_section *t = &pe->sections[i];

		if (!(t->characteristics & IMAGE_SCN_CNT_CODE))
			continue;
		has_code = 1;
		if (elf->entry >= t->rva && elf->entry < (uint64_t)t->rva + t->vsize)
			pe->entry = (uint32_t)elf->entry;
	}
	if (!has_code)
		fail("input has no executable section");
	if (pe->entry == 0)
		fail("entry point %#llx is not in an executable section",
		    (unsigned long long)elf->entry);

	for (i = 1; i < elf->shnum; i++) {
		elf_section(elf, i, &s);
		if (s.type == SHT_REL || s.type == SHT_RELA)
			translate_relocations(pe, elf, &s,
			    elf_section_name(elf, &s));
	}
	if (pe->nrelocs > 1)
		qsort(pe->relocs, pe->nrelocs, sizeof(pe->relocs[0]),
		    compare_u32);
	for (i = 1; i < pe->nrelocs; i++) {
		if (pe->relocs[i] == pe->relocs[i - 1])
			fail("two relocations at %#x", pe->relocs[i]);
	}
}

/* Build .reloc: one block per 4 KiB page, each a multiple of 4 bytes. */
static void
build_reloc_section(struct pe_image *pe)
{
	const struct pe_section *last;
	struct pe_section *reloc;
	unsigned type = pe->is64 ? IMAGE_REL_BASED_DIR64 :
	    IMAGE_REL_BASED_HIGHLOW;
	size_t i = 0, pos = 0, max;
	uint8_t *p;

	if (pe->nsections == 0 || pe->nsections > PE_MAX_SECTIONS - 1)
		fail("unexpected number of sections");
	last = &pe->sections[pe->nsections - 1];
	reloc = &pe->reloc;
	/* Each relocation, plus a header and a pad entry per block. */
	if (pe->nrelocs > (SIZE_MAX - 12) / 12)
		fail("too many relocations");
	max = 12 + pe->nrelocs * 12;
	p = xcalloc(max, 1);
	if (pe->nrelocs == 0) {
		/*
		 * The firmware only relocates an image that has base
		 * relocations, so keep one block of padding entries.
		 */
		put32(p, 0);
		put32(p + 4, 12);
		pos = 12;
	}
	while (i < pe->nrelocs) {
		uint32_t page = pe->relocs[i] & ~(PE_SECTION_ALIGNMENT - 1);
		size_t start = pos;

		pos += 8;
		while (i < pe->nrelocs &&
		    (pe->relocs[i] & ~(PE_SECTION_ALIGNMENT - 1)) == page) {
			put16(p + pos, (type << 12) |
			    (pe->relocs[i] & (PE_SECTION_ALIGNMENT - 1)));
			pos += 2;
			i++;
		}
		if (pos % 4 != 0) {
			put16(p + pos, IMAGE_REL_BASED_ABSOLUTE << 12);
			pos += 2;
		}
		put32(p + start, page);
		put32(p + start + 4, (uint32_t)(pos - start));
	}

	memcpy(reloc->name, ".reloc", sizeof(".reloc"));
	reloc->rva = to_u32(align_up((uint64_t)last->rva + last->vsize,
	    PE_SECTION_ALIGNMENT), ".reloc address");
	reloc->vsize = to_u32(pos, ".reloc size");
	(void)to_u32((uint64_t)reloc->rva + reloc->vsize, ".reloc end");
	reloc->initsize = reloc->vsize;
	reloc->characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA |
	    IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_MEM_READ;
	reloc->data = p;
}

static uint8_t *
write_pe(struct pe_image *pe, size_t *sizep)
{
	unsigned optsize = pe->is64 ? PE32PLUS_OPTIONAL_SIZE :
	    PE32_OPTIONAL_SIZE;
	uint32_t headers, offset, code = 0, idata = 0, udata = 0;
	uint32_t base_of_code = 0, base_of_data = 0, image_size;
	const struct pe_section *reloc = &pe->reloc;
	struct pe_section *all[PE_MAX_SECTIONS];
	uint8_t *out, *coff, *opt, *dir, *sh;
	unsigned i, count;

	if (pe->nsections == 0 || pe->nsections > PE_MAX_SECTIONS - 1)
		fail("unexpected number of sections");
	for (i = 0; i < pe->nsections; i++)
		all[i] = &pe->sections[i];
	all[i] = &pe->reloc;
	count = pe->nsections + 1;
	headers = PE_DOS_HEADER_SIZE + PE_SIGNATURE_SIZE + PE_COFF_HEADER_SIZE +
	    optsize + count * PE_SECTION_HEADER_SIZE;
	headers = to_u32(align_up(headers, PE_FILE_ALIGNMENT), "header size");
	if (pe->sections[0].rva < headers)
		fail("section %s at %#x overlaps the %#x bytes of PE headers",
		    pe->sections[0].name, pe->sections[0].rva, headers);

	offset = headers;
	for (i = 0; i < count; i++) {
		struct pe_section *s = all[i];

		if (s->initsize != 0) {
			s->fileoff = offset;
			s->rawsize = to_u32(align_up(s->initsize,
			    PE_FILE_ALIGNMENT), "section file size");
			offset = to_u32((uint64_t)offset + s->rawsize,
			    "file size");
		}
		if (s->characteristics & IMAGE_SCN_CNT_CODE) {
			code += s->rawsize;
			if (base_of_code == 0)
				base_of_code = s->rva;
		} else if (s->characteristics &
		    IMAGE_SCN_CNT_UNINITIALIZED_DATA) {
			udata += s->vsize;
		} else {
			idata += s->rawsize;
		}
		if (!(s->characteristics & IMAGE_SCN_CNT_CODE) &&
		    base_of_data == 0)
			base_of_data = s->rva;
	}
	image_size = to_u32(align_up((uint64_t)reloc->rva + reloc->vsize,
	    PE_SECTION_ALIGNMENT), "image size");

	out = xcalloc(offset, 1);
	out[0] = 'M';
	out[1] = 'Z';
	put32(out + 0x3c, PE_DOS_HEADER_SIZE);
	memcpy(out + PE_DOS_HEADER_SIZE, "PE\0\0", PE_SIGNATURE_SIZE);

	coff = out + PE_DOS_HEADER_SIZE + PE_SIGNATURE_SIZE;
	put16(coff, pe->is64 ? IMAGE_FILE_MACHINE_AMD64 :
	    IMAGE_FILE_MACHINE_I386);
	put16(coff + 2, count);
	/* TimeDateStamp, PointerToSymbolTable, NumberOfSymbols stay 0. */
	put16(coff + 16, optsize);
	put16(coff + 18, IMAGE_FILE_EXECUTABLE_IMAGE |
	    IMAGE_FILE_LINE_NUMS_STRIPPED | IMAGE_FILE_LOCAL_SYMS_STRIPPED |
	    IMAGE_FILE_DEBUG_STRIPPED |
	    (pe->is64 ? IMAGE_FILE_LARGE_ADDRESS_AWARE :
	    IMAGE_FILE_32BIT_MACHINE));

	opt = coff + PE_COFF_HEADER_SIZE;
	put16(opt, pe->is64 ? IMAGE_NT_OPTIONAL_HDR64_MAGIC :
	    IMAGE_NT_OPTIONAL_HDR32_MAGIC);
	put32(opt + 4, code);
	put32(opt + 8, idata);
	put32(opt + 12, udata);
	put32(opt + 16, pe->entry);
	put32(opt + 20, base_of_code);
	/* ImageBase is 0: the image is linked at 0 (PE32 BaseOfData below). */
	if (!pe->is64)
		put32(opt + 24, base_of_data);
	put32(opt + 32, PE_SECTION_ALIGNMENT);
	put32(opt + 36, PE_FILE_ALIGNMENT);
	put32(opt + 56, image_size);
	put32(opt + 60, headers);
	put16(opt + 68, IMAGE_SUBSYSTEM_EFI_APPLICATION);
	/* Stack and heap sizes are unused by EFI and stay 0. */
	dir = opt + (pe->is64 ? 112 : 96);
	put32(opt + (pe->is64 ? 108 : 92), IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
	put32(dir + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8, reloc->rva);
	put32(dir + IMAGE_DIRECTORY_ENTRY_BASERELOC * 8 + 4, reloc->vsize);

	sh = opt + optsize;
	for (i = 0; i < count; i++, sh += PE_SECTION_HEADER_SIZE) {
		const struct pe_section *s = all[i];

		memcpy(sh, s->name, strlen(s->name));
		put32(sh + 8, s->vsize);
		put32(sh + 12, s->rva);
		put32(sh + 16, s->rawsize);
		put32(sh + 20, s->fileoff);
		put32(sh + 36, s->characteristics);
		if (s->initsize != 0)
			memcpy(out + s->fileoff, s->data, s->initsize);
	}
	*sizep = offset;
	return out;
}

static uint8_t *
read_file(const char *path, size_t *sizep)
{
	FILE *f;
	uint8_t *data = NULL;
	size_t size = 0, alloc = 0;

	f = fopen(path, "rb");
	if (f == NULL)
		fail("%s: %s", path, strerror(errno));
	for (;;) {
		size_t want, n;

		if (size == alloc) {
			uint8_t *p;

			if (alloc > SIZE_MAX / 2)
				fail("%s: too large", path);
			alloc = alloc ? alloc * 2 : 65536;
			p = realloc(data, alloc);
			if (p == NULL)
				fail("out of memory");
			data = p;
		}
		want = alloc - size;
		n = fread(data + size, 1, want, f);
		size += n;
		if (n < want)
			break;
	}
	if (ferror(f))
		fail("%s: read error", path);
	(void)fclose(f);
	*sizep = size;
	return data;
}

static void
write_file(const char *path, const uint8_t *data, size_t size)
{
	FILE *f;

	output_path = path;
	f = fopen(path, "wb");
	if (f == NULL)
		fail("%s: %s", path, strerror(errno));
	if (fwrite(data, 1, size, f) != size) {
		(void)fclose(f);
		fail("%s: write error", path);
	}
	if (fclose(f) != 0)
		fail("%s: %s", path, strerror(errno));
	output_path = NULL;
}

int
main(int argc, char **argv)
{
	struct elf_image elf;
	struct pe_image pe;
	uint8_t *input, *output;
	size_t insize, outsize;
	unsigned i;

	if (argc != 3) {
		fputs("usage: x86-elf2efi input.so output.efi\n", stderr);
		return 2;
	}
	input = read_file(argv[1], &insize);
	elf_open(&elf, input, insize);
	read_elf(&pe, &elf);
	build_reloc_section(&pe);
	output = write_pe(&pe, &outsize);
	write_file(argv[2], output, outsize);

	free(output);
	for (i = 0; i < pe.nsections; i++)
		free(pe.sections[i].data);
	free(pe.reloc.data);
	free(pe.relocs);
	free(input);
	return 0;
}
