/*
 * Copyright (c) 2026 EmberBSD contributors.
 * SPDX-License-Identifier: BSD-2-Clause
 * Origin: EmberBSD libelf/GElf ABI regression.
 */
/* Exercise the public libelf/GElf ABI used by EmberBSD. */

#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gelf.h>
#include <libelf.h>

_Static_assert(sizeof(((Elf_Data *)0)->d_align) == sizeof(uint64_t),
    "Elf_Data.d_align must remain 64 bits");
_Static_assert(sizeof(((Elf_Data *)0)->d_buf) == sizeof(void *),
    "Elf_Data.d_buf must remain a pointer");
_Static_assert(sizeof(((Elf_Data *)0)->d_off) == sizeof(uint64_t),
    "Elf_Data.d_off must remain 64 bits");
_Static_assert(sizeof(((Elf_Data *)0)->d_size) == sizeof(uint64_t),
    "Elf_Data.d_size must remain 64 bits");
_Static_assert(sizeof(((Elf_Data *)0)->d_type) == sizeof(Elf_Type),
    "Elf_Data.d_type must retain Elf_Type representation");
_Static_assert(sizeof(Elf_Type) == sizeof(uint32_t),
    "Elf_Type must remain 32 bits");
_Static_assert(sizeof(((Elf_Data *)0)->d_version) == sizeof(unsigned int),
    "Elf_Data.d_version must retain unsigned int representation");
_Static_assert(sizeof(unsigned int) == sizeof(uint32_t),
    "Elf_Data.d_version must remain 32 bits");
_Static_assert(offsetof(Elf_Data, d_align) == 0,
    "Elf_Data.d_align must remain the first field");
_Static_assert(offsetof(Elf_Data, d_buf) >= sizeof(uint64_t),
    "Elf_Data.d_buf must follow d_align");
_Static_assert(offsetof(Elf_Data, d_off) >=
    offsetof(Elf_Data, d_buf) + sizeof(void *),
    "Elf_Data.d_off must follow d_buf");
_Static_assert(offsetof(Elf_Data, d_size) ==
    offsetof(Elf_Data, d_off) + sizeof(uint64_t),
    "Elf_Data.d_size must follow d_off");
_Static_assert(offsetof(Elf_Data, d_type) ==
    offsetof(Elf_Data, d_size) + sizeof(uint64_t),
    "Elf_Data.d_type must follow d_size");
_Static_assert(offsetof(Elf_Data, d_version) ==
    offsetof(Elf_Data, d_type) + sizeof(Elf_Type),
    "Elf_Data.d_version must follow d_type");
_Static_assert(sizeof(Elf_Data) ==
    ((offsetof(Elf_Data, d_version) + sizeof(unsigned int) +
    _Alignof(Elf_Data) - 1) / _Alignof(Elf_Data)) * _Alignof(Elf_Data),
    "Elf_Data must not gain trailing ABI fields");

static const unsigned char section_bytes[] = {
	0x45, 0x6d, 0x62, 0x65, 0x72, 0x42, 0x53, 0x44
};

static void
fail(const char *message)
{
	fprintf(stderr, "libelf-abi: %s\n", message);
	exit(EXIT_FAILURE);
}

static void
require(int condition, const char *message)
{
	if (!condition)
		fail(message);
}

static int
write_object(int fd, int elfclass)
{
	Elf *elf;
	Elf_Data *data;
	Elf_Scn *scn;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;

	elf = elf_begin(fd, ELF_C_WRITE, NULL);
	if (elf == NULL)
		return (0);
	if (gelf_newehdr(elf, elfclass) == NULL ||
	    gelf_getehdr(elf, &ehdr) == NULL) {
		(void) elf_end(elf);
		return (0);
	}
	ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr.e_type = ET_REL;
	ehdr.e_machine = EM_NONE;
	ehdr.e_version = EV_CURRENT;
	if (gelf_update_ehdr(elf, &ehdr) == 0) {
		(void) elf_end(elf);
		return (0);
	}

	scn = elf_newscn(elf);
	if (scn == NULL || gelf_getshdr(scn, &shdr) == NULL) {
		(void) elf_end(elf);
		return (0);
	}
	shdr.sh_type = SHT_PROGBITS;
	shdr.sh_addralign = 1;
	if (gelf_update_shdr(scn, &shdr) == 0) {
		(void) elf_end(elf);
		return (0);
	}

	data = elf_newdata(scn);
	if (data == NULL) {
		(void) elf_end(elf);
		return (0);
	}
	data->d_align = 1;
	data->d_buf = (void *)section_bytes;
	data->d_off = 0;
	data->d_size = sizeof(section_bytes);
	data->d_type = ELF_T_BYTE;
	data->d_version = EV_CURRENT;

	if (elf_update(elf, ELF_C_WRITE) < 0) {
		(void) elf_end(elf);
		return (0);
	}
	return (elf_end(elf) >= 0);
}

static void
read_object(const char *path, int elfclass)
{
	Elf *elf;
	const Elf_Data *data;
	Elf_Scn *scn;
	GElf_Ehdr ehdr;
	GElf_Shdr shdr;
	const char *ident;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		fail("open generated ELF object");
	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (elf == NULL)
		fail("open generated ELF object with elf_begin");
	ident = elf_getident(elf, NULL);
	if (ident == NULL || (unsigned char)ident[EI_CLASS] != elfclass ||
	    (unsigned char)ident[EI_DATA] != ELFDATA2LSB)
		fail("read ELF identification class and encoding");
	if (gelf_getehdr(elf, &ehdr) == NULL || ehdr.e_type != ET_REL ||
	    ehdr.e_version != EV_CURRENT)
		fail("read ELF header through GElf");
	scn = elf_getscn(elf, 1);
	if (scn == NULL || gelf_getshdr(scn, &shdr) == NULL ||
	    shdr.sh_type != SHT_PROGBITS)
		fail("read ELF section header through GElf");
	data = elf_getdata(scn, NULL);
	if (data == NULL || data->d_type != ELF_T_BYTE ||
	    data->d_size != sizeof(section_bytes) ||
	    memcmp(data->d_buf, section_bytes, sizeof(section_bytes)) != 0)
		fail("read ELF section data");
	if (elf_end(elf) < 0)
		fail("close ELF descriptor");
	if (close(fd) < 0)
		fail("close ELF file");
}

static void
check_class(int elfclass)
{
	char path[] = "libelf-abi-XXXXXX";
	int fd;

	fd = mkstemp(path);
	if (fd < 0)
		fail("create temporary ELF object");
	if (!write_object(fd, elfclass)) {
		(void) close(fd);
		(void) unlink(path);
		fail("write ELF header and section through libelf/GElf");
	}
	if (close(fd) < 0)
		fail("close written ELF file");
	read_object(path, elfclass);
	if (unlink(path) < 0)
		fail("remove temporary ELF object");
}

static void
check_malformed_input(void)
{
	unsigned char malformed[EI_NIDENT + 1] = { 0 };
	const Elf *elf;

	malformed[EI_MAG0] = ELFMAG0;
	malformed[EI_MAG1] = ELFMAG1;
	malformed[EI_MAG2] = ELFMAG2;
	malformed[EI_MAG3] = ELFMAG3;
	malformed[EI_CLASS] = 0;
	malformed[EI_DATA] = ELFDATA2LSB;
	malformed[EI_VERSION] = EV_CURRENT;
	(void) elf_errno();
	elf = elf_memory((char *)malformed, sizeof(malformed));
	require(elf == NULL, "reject malformed ELF identification");
	require(elf_errno() != 0, "report malformed ELF identification");
}

int
main(void)
{
	require(elf_version(EV_CURRENT) == EV_CURRENT,
	    "initialize current libelf API version");
	printf("Elf_Data size=%zu align=%zu buf=%zu off=%zu data_size=%zu "
	    "type=%zu version=%zu\n",
	    sizeof(Elf_Data), offsetof(Elf_Data, d_align),
	    offsetof(Elf_Data, d_buf), offsetof(Elf_Data, d_off),
	    offsetof(Elf_Data, d_size), offsetof(Elf_Data, d_type),
	    offsetof(Elf_Data, d_version));
	check_class(ELFCLASS32);
	check_class(ELFCLASS64);
	check_malformed_input();
	puts("PASS libelf ABI/runtime probe: ELF32, ELF64, malformed input");
	return (EXIT_SUCCESS);
}
