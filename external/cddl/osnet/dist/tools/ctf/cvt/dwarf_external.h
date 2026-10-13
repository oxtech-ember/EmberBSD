/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), identity-checked external DWARF objects. */
#ifndef _CTF_DWARF_EXTERNAL_H_
#define _CTF_DWARF_EXTERNAL_H_

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <gelf.h>

struct dw_external {
	struct dw_external *next;
	char *path;
	int fd;
	Elf *elf;
	Dwarf_Debug dbg;
};
struct dw_sup {
	const char *name;
	const unsigned char *checksum;
	size_t length;
	unsigned int role;
};

static Elf_Data *
dw_section(Elf *elf, const char *wanted)
{
	Elf_Scn *scn = NULL;
	Elf_Data *data = NULL;
	GElf_Shdr sh;
	size_t names;
	const char *name;

	if (elf_getshstrndx(elf, &names) == 0)
		terminate("cannot read external DWARF section names\n");
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		if (gelf_getshdr(scn, &sh) == NULL ||
		    (name = elf_strptr(elf, names, sh.sh_name)) == NULL)
			terminate("invalid external DWARF section\n");
		if (strcmp(name, wanted) != 0)
			continue;
		if (data != NULL)
			terminate("duplicate %s section\n", wanted);
		data = elf_getdata(scn, NULL);
		if (data == NULL || data->d_buf == NULL ||
		    data->d_size != sh.sh_size)
			terminate("invalid %s section data\n", wanted);
	}
	return data;
}

/* DWARF5 7.3.6: the checksum is an opaque producer-defined identifier. */
static int
dw_sup_read(Elf *elf, struct dw_sup *sup)
{
	Elf_Data *data = dw_section(elf, ".debug_sup");
	GElf_Ehdr eh;
	const unsigned char *p, *end, *nul;
	uint64_t length = 0;
	unsigned int shift = 0, byte, version;

	if (data == NULL)
		return 0;
	p = data->d_buf;
	end = p + data->d_size;
	if (data->d_size < 5 || gelf_getehdr(elf, &eh) == NULL)
		goto invalid;
	version = eh.e_ident[EI_DATA] == ELFDATA2LSB ?
	    p[0] | p[1] << 8 : p[0] << 8 | p[1];
	if (version != 5 || p[2] > 1)
		goto invalid;
	sup->role = p[2];
	p += 3;
	sup->name = (const char *)p;
	nul = memchr(p, 0, end - p);
	if (nul == NULL || (sup->role == 1 && nul != p) ||
	    (sup->role == 0 && nul == p))
		goto invalid;
	p = nul + 1;
	do {
		if (p == end || shift >= 64)
			goto invalid;
		byte = *p++;
		if (shift == 63 && (byte & 0x7e) != 0)
			goto invalid;
		length |= (uint64_t)(byte & 0x7f) << shift;
		shift += 7;
	} while (byte & 0x80);
	if (length != (uint64_t)(end - p))
		goto invalid;
	sup->checksum = p;
	sup->length = length;
	return 1;
invalid:
	terminate("invalid .debug_sup header\n");
	return 0;
}

static char *
dw_join(const char *directory, const char *name)
{
	char *path;
	size_t len;

	if (name[0] == '/')
		return xstrdup(name);
	len = strlen(directory) + strlen(name) + 2;
	path = xmalloc(len);
	(void)snprintf(path, len, "%s/%s", directory, name);
	return path;
}

static void
dw_external_open(struct dw_external *ext, Elf *mainelf, const char *filename,
    const char *name, const char *compdir, int package)
{
	GElf_Ehdr mainhdr, hdr;
	Dwarf_Error error;
	struct stat st;
	char *copy = xstrdup(filename);
	char *path = dw_join(dirname(copy), name);

	free(copy);
	ext->fd = open(path, O_RDONLY);
	if (ext->fd < 0 && errno == ENOENT && name[0] != '/' && compdir != NULL) {
		free(path);
		path = dw_join(compdir, name);
		ext->fd = open(path, O_RDONLY);
	}
	if (ext->fd < 0 && errno == ENOENT && package) {
		free(path);
		path = xmalloc(strlen(filename) + 5);
		(void)sprintf(path, "%s.dwp", filename);
		ext->fd = open(path, O_RDONLY);
	}
	if (ext->fd < 0)
		terminate("cannot open external DWARF %s: %s\n", path, strerror(errno));
	if (fstat(ext->fd, &st) != 0 || !S_ISREG(st.st_mode))
		terminate("external DWARF is not a regular file: %s\n", path);
	ext->elf = elf_begin(ext->fd, ELF_C_READ, NULL);
	if (ext->elf == NULL || gelf_getehdr(ext->elf, &hdr) == NULL ||
	    gelf_getehdr(mainelf, &mainhdr) == NULL ||
	    hdr.e_ident[EI_CLASS] != mainhdr.e_ident[EI_CLASS] ||
	    hdr.e_ident[EI_DATA] != mainhdr.e_ident[EI_DATA] ||
	    hdr.e_machine != mainhdr.e_machine)
		terminate("external DWARF ELF identity mismatch: %s\n", path);
	if (dwarf_elf_init(ext->elf, DW_DLC_READ, NULL, NULL, &ext->dbg,
	    &error) != DW_DLV_OK)
		terminate("cannot initialize external DWARF %s: %s\n", path,
		    dwarf_errmsg(error));
	ext->path = path;
}

static void
dw_external_close(struct dw_external *ext)
{
	Dwarf_Error error;

	if (ext->dbg == NULL)
		return;
	(void)dwarf_finish(ext->dbg, &error);
	(void)elf_end(ext->elf);
	(void)close(ext->fd);
	free(ext->path);
}
#endif
