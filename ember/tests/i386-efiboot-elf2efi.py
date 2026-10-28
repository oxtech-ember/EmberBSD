#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

"""Convert the x86 EFI boot loaders from ELF to PE with x86-elf2efi.

llvm-objcopy cannot write PE images, so the clang/ld.lld profile links
bootx64.efi and bootia32.efi as position independent ELF images at address 0
and converts them with the in-tree host tool x86-elf2efi. The tool maps the
sections at their link addresses, turns R_X86_64_RELATIVE and R_386_RELATIVE
into PE base relocations and refuses every other relocation.

The converter tests feed the tool small ELF files made here and check the
relocation translation, the rejections and the PE header fields. The build
tests build both loaders from the real makefiles and check the PE headers,
the sections and the base relocation table against the linked ELF image.
Also checks that only the ld.lld profile uses the converter and that the
host tool is part of the amd64 and i386 tools build.
"""

import argparse
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest

TESTS = Path(__file__).resolve().parent
ARGS = None
TOOL = None
TOOL_WORK = None

EM_386, EM_X86_64 = 3, 62
ET_EXEC, ET_DYN = 2, 3
SHT_PROGBITS, SHT_STRTAB, SHT_RELA, SHT_NOBITS, SHT_REL = 1, 3, 4, 8, 9
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 1, 2, 4
RELATIVE = 8
R_X86_64_64, R_X86_64_GLOB_DAT, R_X86_64_JUMP_SLOT, R_386_32 = 1, 6, 7, 1

SCN_CODE, SCN_IDATA, SCN_UDATA = 0x20, 0x40, 0x80
SCN_DISCARDABLE, SCN_EXECUTE, SCN_READ, SCN_WRITE = 0x02000000, 0x20000000, 0x40000000, 0x80000000
X86_BOOT_MAGIC_EFI = ord("x") << 24 | 0x86b << 12 | ord("m") << 4 | 5


def make_command(directory, objects, family="clang", machine="amd64"):
    arch = {"amd64": "x86_64", "i386": "i386", "evbarm": "aarch64"}[machine]
    return [str(ARGS.make), "-m", str(ARGS.source / "share/mk"), "-C", str(ARGS.source / directory),
            "MACHINE=" + machine, "MACHINE_ARCH=" + arch, "USETOOLS=yes", "TOOLCHAIN_MISSING=yes",
            "MKBINUTILS=no", "EMBER_COMPILER_FAMILY=" + family,
            "EXTERNAL_TOOLCHAIN=" + str(ARGS.toolchain), "TOOLDIR=" + str(ARGS.tooldir),
            "NETBSDSRCDIR=" + str(ARGS.source), "EMBER_LLVM_SOURCE=" + str(ARGS.llvm_source),
            "DESTDIR=" + str(objects / "destdir"), "MKREPRO=no"]


def environment(objects):
    return {"PATH": "/usr/bin:/bin", "HOME": tempfile.gettempdir(),
            "MAKEOBJDIRPREFIX": str(objects)}


def make_values(directory, objects, *names, **options):
    command = make_command(directory, objects, **options)
    for name in names:
        command += ["-V", "${%s}" % name]
    result = subprocess.run(command, text=True, capture_output=True,
                            env=environment(objects), timeout=60)
    if result.returncode:
        raise AssertionError(result.stderr)
    return [shlex.split(line) for line in result.stdout.splitlines()]


def run_make(directory, objects, *goals, extra=()):
    command = make_command(directory, objects) + list(extra)
    output, status = "", 0
    for goal in (["obj"], list(goals)):
        built = subprocess.run(command + goal, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, env=environment(objects), timeout=3600)
        output += built.stdout
        status = status or built.returncode
    return status, output


def setUpModule():
    global TOOL, TOOL_WORK
    TOOL_WORK = tempfile.TemporaryDirectory()
    objects = Path(TOOL_WORK.name)
    status, output = run_make("tools/x86-elf2efi", objects, "dependall",
                              extra=["HOST_CC=" + str(ARGS.toolchain / "bin/clang")])
    if status:
        raise AssertionError(output[-6000:])
    TOOL = objects / str(ARGS.source).lstrip("/") / "tools/x86-elf2efi/x86-elf2efi"
    if not TOOL.is_file():
        raise AssertionError("x86-elf2efi was not built:\n" + output[-3000:])


def tearDownModule():
    TOOL_WORK.cleanup()


# ELF input files -----------------------------------------------------------

def section(name, kind, flags, addr, data=b"", size=None, align=16, entsize=0):
    return {"name": name, "type": kind, "flags": flags, "addr": addr, "data": data,
            "size": len(data) if size is None else size, "align": align, "entsize": entsize}


def relocations(is64, rela, entries):
    """entries: (offset, type, symbol, addend)."""
    out = b""
    for offset, kind, symbol, addend in entries:
        if is64:
            out += struct.pack("<QQ", offset, symbol << 32 | kind)
            if rela:
                out += struct.pack("<q", addend)
        else:
            out += struct.pack("<II", offset, symbol << 8 | kind)
            if rela:
                out += struct.pack("<i", addend)
    return out


def rel_section(is64, rela, entries, flags=SHF_ALLOC, addr=0x8000):
    entsize = (24 if rela else 16) if is64 else (12 if rela else 8)
    name = ".rela.dyn" if rela else ".rel.dyn"
    return section(name, SHT_RELA if rela else SHT_REL, flags, addr,
                   relocations(is64, rela, entries), align=8, entsize=entsize)


def make_elf(is64, sections, entry, machine=None, kind=ET_DYN):
    machine = machine if machine is not None else (EM_X86_64 if is64 else EM_386)
    names = b"\0"
    offsets = []
    for s in sections + [{"name": ".shstrtab"}]:
        offsets.append(len(names))
        names += s["name"].encode() + b"\0"
    ehsize, shentsize = (64, 64) if is64 else (52, 40)
    body = bytearray(ehsize)
    file_offsets = []
    for s in sections:
        while len(body) % 16:
            body.append(0)
        file_offsets.append(len(body))
        body += s["data"]
    names_offset = len(body)
    body += names
    while len(body) % 8:
        body.append(0)
    shoff = len(body)
    headers = [bytes(shentsize)]
    rows = [(offsets[i], s["type"], s["flags"], s["addr"], file_offsets[i], s["size"], s["align"],
             s["entsize"]) for i, s in enumerate(sections)]
    rows.append((offsets[-1], SHT_STRTAB, 0, 0, names_offset, len(names), 1, 0))
    for name, stype, flags, addr, offset, size, align, entsize in rows:
        if is64:
            headers.append(struct.pack("<IIQQQQIIQQ", name, stype, flags, addr, offset, size,
                                       0, 0, align, entsize))
        else:
            headers.append(struct.pack("<IIIIIIIIII", name, stype, flags, addr, offset, size,
                                       0, 0, align, entsize))
    body += b"".join(headers)
    ident = b"\x7fELF" + bytes([2 if is64 else 1, 1, 1]) + bytes(9)
    count, shstrndx = len(headers), len(headers) - 1
    if is64:
        header = ident + struct.pack("<HHIQQQIHHHHHH", kind, machine, 1, entry, 0, shoff, 0,
                                     ehsize, 0, 0, shentsize, count, shstrndx)
    else:
        header = ident + struct.pack("<HHIIIIIHHHHHH", kind, machine, 1, entry, 0, shoff, 0,
                                     ehsize, 0, 0, shentsize, count, shstrndx)
    body[:ehsize] = header
    return bytes(body)


def text(is64=True, addr=0x1000, size=0x10):
    return section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, addr, b"\xc3" * size)


def data(addr=0x2000, size=0x1010, fill=b"\0"):
    return section(".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, addr, fill * size)


# PE output files -----------------------------------------------------------

class PE:
    def __init__(self, image):
        self.image = image
        assert image[:2] == b"MZ", "no DOS header"
        self.lfanew = struct.unpack_from("<I", image, 0x3c)[0]
        assert image[self.lfanew:self.lfanew + 4] == b"PE\0\0", "no PE signature"
        coff = self.lfanew + 4
        (self.machine, count, _, _, _, optsize,
         self.characteristics) = struct.unpack_from("<HHIIIHH", image, coff)
        opt = coff + 20
        self.magic = struct.unpack_from("<H", image, opt)[0]
        self.is64 = self.magic == 0x20b
        (self.size_of_code, self.size_of_idata, self.size_of_udata, self.entry,
         self.base_of_code) = struct.unpack_from("<IIIII", image, opt + 4)
        if self.is64:
            self.base_of_data = None
            self.image_base = struct.unpack_from("<Q", image, opt + 24)[0]
        else:
            self.base_of_data, self.image_base = struct.unpack_from("<II", image, opt + 24)
        self.section_alignment, self.file_alignment = struct.unpack_from("<II", image, opt + 32)
        self.size_of_image, self.size_of_headers = struct.unpack_from("<II", image, opt + 56)
        self.subsystem = struct.unpack_from("<H", image, opt + 68)[0]
        ndir_at = opt + (108 if self.is64 else 92)
        ndir = struct.unpack_from("<I", image, ndir_at)[0]
        self.directories = [struct.unpack_from("<II", image, ndir_at + 4 + 8 * i)
                            for i in range(ndir)]
        self.sections = []
        for i in range(count):
            at = opt + optsize + 40 * i
            name = image[at:at + 8].rstrip(b"\0").decode()
            vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", image, at + 8)
            flags = struct.unpack_from("<I", image, at + 36)[0]
            self.sections.append({"name": name, "vsize": vsize, "rva": rva, "rawsize": rawsize,
                                  "rawptr": rawptr, "flags": flags})

    def section(self, name):
        return next(s for s in self.sections if s["name"] == name)

    def read(self, rva, size):
        for s in self.sections:
            if s["rva"] <= rva and rva + size <= s["rva"] + s["rawsize"]:
                at = s["rawptr"] + rva - s["rva"]
                return self.image[at:at + size]
        raise AssertionError("RVA %#x is not in the file" % rva)

    def blocks(self):
        """The base relocation blocks as (page, block size, [(type, offset)])."""
        rva, size = self.directories[5]
        table = self.read(rva, size)
        out, at = [], 0
        while at < len(table):
            page, block = struct.unpack_from("<II", table, at)
            entries = [(e >> 12, e & 0xfff) for e in
                       struct.unpack_from("<%dH" % ((block - 8) // 2), table, at + 8)]
            out.append((page, block, entries))
            at += block
        return out

    def relocated(self):
        kind = 10 if self.is64 else 3
        rvas = []
        for page, _, entries in self.blocks():
            for etype, offset in entries:
                if etype == kind:
                    rvas.append(page + offset)
                elif etype != 0:
                    raise AssertionError("unexpected base relocation type %d" % etype)
        return rvas


def convert(elf):
    with tempfile.TemporaryDirectory() as work:
        source, target = Path(work) / "in.so", Path(work) / "out.efi"
        source.write_bytes(elf)
        result = subprocess.run([str(TOOL), str(source), str(target)], text=True,
                                capture_output=True, timeout=60)
        image = target.read_bytes() if target.exists() else None
        return result, image


class ConverterTests(unittest.TestCase):
    def converted(self, elf):
        result, image = convert(elf)
        self.assertEqual(result.returncode, 0, result.stderr)
        return PE(image)

    def rejected(self, elf, message):
        result, image = convert(elf)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(message, result.stderr)
        self.assertIsNone(image, "a failed conversion left an output file")

    def test_x86_64_relative_relocations_become_dir64_entries(self):
        relocs = [(0x2008, RELATIVE, 0, 0x1234), (0x2000, RELATIVE, 0, 0x1000),
                  (0x3008, RELATIVE, 0, 0x2010)]
        pe = self.converted(make_elf(True, [text(), data(), rel_section(True, True, relocs)],
                                     0x1004))
        # ld.lld leaves RELA addends out of the data; the converter puts
        # them in place, since the image is linked at 0.
        for offset, _, _, addend in relocs:
            self.assertEqual(struct.unpack("<Q", pe.read(offset, 8))[0], addend)
        self.assertEqual(pe.blocks(), [(0x2000, 12, [(10, 0), (10, 8)]),
                                       (0x3000, 12, [(10, 8), (0, 0)])])
        self.assertEqual(sorted(pe.relocated()), sorted(r[0] for r in relocs))

    def test_i386_relative_relocations_become_highlow_entries(self):
        contents = bytearray(0x1010)
        struct.pack_into("<I", contents, 0x10, 0x1008)
        struct.pack_into("<I", contents, 0x20, 0x2004)
        struct.pack_into("<I", contents, 0x30, 0x1000)
        sections = [text(False), section(".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0x2000,
                                         bytes(contents)),
                    rel_section(False, False, [(0x2010, RELATIVE, 0, 0), (0x2030, RELATIVE, 0, 0),
                                               (0x2020, RELATIVE, 0, 0)])]
        pe = self.converted(make_elf(False, sections, 0x1000))
        # REL addends stay where the linker put them.
        self.assertEqual(struct.unpack("<I", pe.read(0x2010, 4))[0], 0x1008)
        self.assertEqual(struct.unpack("<I", pe.read(0x2020, 4))[0], 0x2004)
        self.assertEqual(pe.blocks(), [(0x2000, 16, [(3, 0x10), (3, 0x20), (3, 0x30), (0, 0)])])

    def test_relocations_on_many_pages_get_one_block_per_page(self):
        relocs = [(0x2000 + 0x1000 * page + 8 * i, RELATIVE, 0, i) for page in range(4)
                  for i in range(page + 1)]
        pe = self.converted(make_elf(True, [text(), data(size=0x4000),
                                            rel_section(True, True, relocs)], 0x1000))
        blocks = pe.blocks()
        self.assertEqual([b[0] for b in blocks], [0x2000, 0x3000, 0x4000, 0x5000])
        for page, (_, size, entries) in enumerate(blocks):
            self.assertEqual(size % 4, 0)
            self.assertEqual(len([e for e in entries if e[0] == 10]), page + 1)
        self.assertEqual(len(pe.relocated()), len(relocs))

    def test_an_image_without_relocations_keeps_a_padding_block(self):
        pe = self.converted(make_elf(True, [text(), data()], 0x1000))
        self.assertEqual(pe.blocks(), [(0, 12, [(0, 0), (0, 0)])])
        self.assertEqual(pe.directories[5][1], 12)

    def check_headers(self, pe, is64):
        self.assertEqual(pe.machine, 0x8664 if is64 else 0x14c)
        self.assertEqual(pe.magic, 0x20b if is64 else 0x10b)
        self.assertEqual(pe.subsystem, 10)
        self.assertEqual(pe.image_base, 0)
        self.assertEqual(pe.section_alignment, 0x1000)
        self.assertEqual(pe.file_alignment, 0x200)
        self.assertEqual(pe.characteristics & 0x0002, 0x0002, "not an executable image")
        self.assertEqual(pe.characteristics & 0x0020, 0x0020 if is64 else 0)
        self.assertEqual(pe.characteristics & 0x0100, 0 if is64 else 0x0100)
        self.assertEqual(len(pe.directories), 16)
        self.assertEqual(pe.size_of_headers % 0x200, 0)
        last = pe.sections[-1]
        self.assertEqual(last["name"], ".reloc")
        self.assertEqual(pe.directories[5], (last["rva"], last["vsize"]))
        self.assertEqual(last["flags"], SCN_IDATA | SCN_DISCARDABLE | SCN_READ)
        self.assertEqual(pe.size_of_image,
                         (last["rva"] + last["vsize"] + 0xfff) & ~0xfff)
        for s in pe.sections:
            self.assertEqual(s["rva"] % 0x1000, 0)
            self.assertEqual(s["rawptr"] % 0x200, 0)
            self.assertEqual(s["rawsize"] % 0x200, 0)
            self.assertGreaterEqual(s["rva"], pe.size_of_headers)

    def test_header_fields(self):
        for is64 in (True, False):
            with self.subTest(is64=is64):
                bss = section(".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 0x4000, size=0x100)
                pe = self.converted(make_elf(is64, [text(is64), data(), bss], 0x1008))
                self.check_headers(pe, is64)
                self.assertEqual(pe.entry, 0x1008)
                self.assertEqual(pe.base_of_code, 0x1000)
                self.assertEqual(pe.base_of_data, None if is64 else 0x2000)
                self.assertEqual([s["name"] for s in pe.sections],
                                 [".text", ".data", ".bss", ".reloc"])
                self.assertEqual(pe.section(".text")["flags"], SCN_CODE | SCN_EXECUTE | SCN_READ)
                self.assertEqual(pe.section(".data")["flags"], SCN_IDATA | SCN_READ | SCN_WRITE)
                self.assertEqual(pe.section(".bss")["flags"], SCN_UDATA | SCN_READ | SCN_WRITE)
                self.assertEqual(pe.section(".bss")["rawsize"], 0)
                self.assertEqual(pe.section(".reloc")["rva"], 0x5000)
                self.assertEqual(pe.read(0x1000, 0x10), b"\xc3" * 0x10)
                self.assertEqual(pe.size_of_code, 0x200)
                self.assertEqual(pe.size_of_udata, 0x100)

    def test_other_relocation_types_are_rejected(self):
        cases = [(True, True, R_X86_64_64), (True, True, R_X86_64_GLOB_DAT),
                 (True, True, R_X86_64_JUMP_SLOT), (False, False, R_386_32)]
        for is64, rela, kind in cases:
            with self.subTest(is64=is64, type=kind):
                elf = make_elf(is64, [text(is64), data(),
                                      rel_section(is64, rela, [(0x2000, kind, 0, 0)])], 0x1000)
                self.rejected(elf, "relocation type %d" % kind)

    def test_relative_relocation_with_a_symbol_is_rejected(self):
        elf = make_elf(True, [text(), data(), rel_section(True, True, [(0x2000, RELATIVE, 1, 0)])],
                       0x1000)
        self.rejected(elf, "relocation type 8 (symbol 1)")

    def test_static_relocation_sections_are_rejected(self):
        elf = make_elf(True, [text(), data(),
                              rel_section(True, True, [(0x2000, RELATIVE, 0, 0)], flags=0, addr=0)],
                       0x1000)
        self.rejected(elf, "static relocation section")

    def test_relocations_outside_the_initialized_data_are_rejected(self):
        bss = section(".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 0x4000, size=0x100)
        for offset in (0x4000, 0x300c, 0x9000):
            with self.subTest(offset=hex(offset)):
                elf = make_elf(True, [text(), data(), bss,
                                      rel_section(True, True, [(offset, RELATIVE, 0, 0)])], 0x1000)
                self.rejected(elf, "outside the initialized image data")

    def test_duplicate_relocations_are_rejected(self):
        elf = make_elf(True, [text(), data(), rel_section(
            True, True, [(0x2000, RELATIVE, 0, 0), (0x2000, RELATIVE, 0, 0)])], 0x1000)
        self.rejected(elf, "two relocations")

    def test_section_layout_is_checked(self):
        self.rejected(make_elf(True, [text(), data(addr=0x2100)], 0x1000), "is not aligned to")
        self.rejected(make_elf(True, [text(), data(addr=0x3000)], 0x1000), "does not follow")
        big = text()
        big["align"] = 0x2000
        self.rejected(make_elf(True, [big, data()], 0x1000), "needs alignment")
        self.rejected(make_elf(True, [text(addr=0)], 4), "overlaps")
        reloc = data()
        reloc["name"] = ".reloc"
        self.rejected(make_elf(True, [text(), reloc], 0x1000), "already has a .reloc")
        self.rejected(make_elf(True, [data()], 0x2000), "no executable section")

    def test_wrong_inputs_are_rejected(self):
        self.rejected(make_elf(True, [text(), data()], 0x1000, machine=EM_386), "ELF machine 3")
        self.rejected(make_elf(False, [text(False), data()], 0x1000, machine=EM_X86_64),
                      "ELF machine 62")
        self.rejected(make_elf(True, [text(), data()], 0x1000, kind=ET_EXEC), "is not ET_DYN")
        self.rejected(make_elf(True, [text(), data()], 0x2000), "entry point 0x2000")
        self.rejected(b"MZ" + bytes(100), "not an ELF file")
        self.rejected(make_elf(True, [text(), data()], 0x1000)[:200], "truncated")

    def test_usage(self):
        result = subprocess.run([str(TOOL)], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage:", result.stderr)


class ProfileTests(unittest.TestCase):
    EFIBOOT = ("sys/arch/i386/stand/efiboot/bootx64", "sys/arch/i386/stand/efiboot/bootia32")

    def test_only_the_lld_profile_converts_with_the_host_tool(self):
        with tempfile.TemporaryDirectory() as work:
            objects = Path(work)
            for directory in self.EFIBOOT:
                with self.subTest(directory=directory):
                    clang = make_values(directory, objects, "EFIBOOT_ELF2EFI", "LDSCRIPT",
                                        "CPPFLAGS", "SRCS")
                    self.assertEqual(clang[0], ["yes"])
                    self.assertEqual(Path(clang[1][0]).name, "ldscript.lld")
                    self.assertIn("-DEFIBOOT_PE_RELOCATED", clang[2])
                    self.assertNotIn("self_reloc.c", clang[3])
                    gcc = make_values(directory, objects, "EFIBOOT_ELF2EFI", "LDSCRIPT",
                                      "CPPFLAGS", "SRCS", family="gcc")
                    self.assertEqual(gcc[0], [])
                    self.assertEqual(Path(gcc[1][0]).name, "ldscript")
                    self.assertNotIn("-DEFIBOOT_PE_RELOCATED", gcc[2])
                    self.assertIn("self_reloc.c", gcc[3])

    def test_the_tool_is_built_with_the_x86_host_tools(self):
        with tempfile.TemporaryDirectory() as work:
            objects = Path(work)
            for machine, wanted in (("amd64", True), ("i386", True), ("evbarm", False)):
                with self.subTest(machine=machine):
                    subdirs = make_values("tools", objects, "SUBDIR", machine=machine)[0]
                    self.assertEqual("x86-elf2efi" in subdirs, wanted)


class EfibootBuildTests(unittest.TestCase):
    LOADERS = {"bootx64": True, "bootia32": False}

    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory()
        cls.objects = Path(cls.work.name)
        cls.status, cls.output = run_make("sys/arch/i386/stand/efiboot", cls.objects,
                                          "-j4", "dependall", extra=["TOOL_X86ELF2EFI=" + str(TOOL)])
        cls.objdir = cls.objects / str(ARGS.source).lstrip("/") / "sys/arch/i386/stand/efiboot"

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def elf_relocations(self, path):
        listing = subprocess.run([str(ARGS.toolchain / "bin/llvm-readelf"), "-rW", str(path)],
                                 text=True, capture_output=True, check=True).stdout
        relocs = {}
        for line in listing.splitlines():
            fields = line.split()
            if len(fields) >= 3 and re.fullmatch(r"[0-9a-f]{8,16}", fields[0]):
                self.assertIn(fields[2], ("R_X86_64_RELATIVE", "R_386_RELATIVE"), line)
                relocs[int(fields[0], 16)] = int(fields[3], 16) if len(fields) > 3 else None
        return relocs

    def elf_entry(self, path):
        headers = subprocess.run([str(ARGS.toolchain / "bin/llvm-readelf"), "-h", str(path)],
                                 text=True, capture_output=True, check=True).stdout
        return int(re.search(r"Entry point address:\s+(0x[0-9a-f]+)", headers).group(1), 16)

    def test_build_succeeds_with_the_converter(self):
        self.assertEqual(self.status, 0, self.output[-6000:])
        for name in self.LOADERS:
            self.assertRegex(self.output, r"x86-elf2efi %s\.efi\.so %s\.efi" % (name, name))
            self.assertNotRegex(self.output, r"objcopy[^\n]*%s\.efi" % name)

    def test_loaders_are_pe_efi_applications(self):
        for name, is64 in self.LOADERS.items():
            with self.subTest(loader=name):
                path = self.objdir / name / (name + ".efi")
                pe = PE(path.read_bytes())
                ConverterTests.check_headers(self, pe, is64)
                self.assertEqual([s["name"] for s in pe.sections], [".text", ".data", ".reloc"])
                text_section = pe.section(".text")
                self.assertEqual(text_section["flags"], SCN_CODE | SCN_EXECUTE | SCN_READ)
                self.assertEqual(pe.entry, self.elf_entry(path.with_name(name + ".efi.so")))
                self.assertTrue(text_section["rva"] <= pe.entry
                                < text_section["rva"] + text_section["vsize"])
                # installboot finds the EFI loader by the magic after the
                # jump at the start of a 512-byte block.
                image = path.read_bytes()
                magic = struct.pack("<I", X86_BOOT_MAGIC_EFI)
                self.assertEqual(image.count(magic), 1)
                self.assertEqual(image.find(magic) % 512, 4)

    def test_base_relocations_match_the_elf_relocations(self):
        for name, is64 in self.LOADERS.items():
            with self.subTest(loader=name):
                path = self.objdir / name / (name + ".efi")
                relocs = self.elf_relocations(path.with_name(name + ".efi.so"))
                self.assertGreater(len(relocs), 100)
                pe = PE(path.read_bytes())
                rvas = pe.relocated()
                self.assertEqual(sorted(rvas), sorted(relocs))
                for _, size, _ in pe.blocks():
                    self.assertEqual(size % 4, 0)
                for rva, addend in relocs.items():
                    word = 8 if is64 else 4
                    value = int.from_bytes(pe.read(rva, word), "little")
                    if addend is not None:
                        self.assertEqual(value, addend, hex(rva))
                    self.assertLess(value, pe.size_of_image, hex(rva))


def main():
    global ARGS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", required=True, type=Path, help="host bmake")
    parser.add_argument("--toolchain", required=True, type=Path, help="LLVM prefix (EXTERNAL_TOOLCHAIN)")
    parser.add_argument("--tooldir", required=True, type=Path, help="OS host tools")
    parser.add_argument("--llvm-source", required=True, type=Path,
                        help="Ports-prepared LLVM source root (EMBER_LLVM_SOURCE)")
    parser.add_argument("--source", type=Path, default=TESTS.parents[1],
                        help="OS source tree (default: the tree holding this test)")
    ARGS, rest = parser.parse_known_args()
    ARGS.source = ARGS.source.resolve()
    unittest.main(argv=[sys.argv[0]] + rest)


if __name__ == "__main__":
    main()
