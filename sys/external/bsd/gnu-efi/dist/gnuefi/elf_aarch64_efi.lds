/*	$NetBSD: elf_aarch64_efi.lds,v 1.3 2020/04/26 06:46:46 rin Exp $	*/

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)
/* EmberBSD: explicit PHDRS avoid an implicit uncovered PT_PHDR with new
 * binutils. ELF headers are not part of the objcopy-produced PE image.
 * Keep ImageBase at zero: crt0 passes its runtime address to _relocate,
 * whose RELATIVE offsets and addends are ELF addresses. */
PHDRS
{
  text PT_LOAD;
}
SECTIONS
{
  .text 0 : {
    _text = .;
    *(.text.head)
    *(.text)
    *(.text.*)
    *(.gnu.linkonce.t.*)
    *(.srodata)
    *(.rodata*)
    . = ALIGN(16);
  } :text
  _etext = .;
  _text_size = . - _text;
  .dynamic  : { *(.dynamic) } :text
  .data : ALIGN(4096)
  {
   _data = .;
   *(.sdata)
   *(.data)
   *(.data1)
   *(.data.*)
   *(.got.plt)
   *(.got)

   /* the EFI loader doesn't seem to like a .bss section, so we stick
      it all into .data: */
   . = ALIGN(16);
   _bss = .;
   *(.sbss)
   *(.scommon)
   *(.dynbss)
   *(.bss)
   *(COMMON)
   . = ALIGN(16);
   _bss_end = .;
  } :text

  .rela.dyn : { *(.rela.dyn) } :text
  .rela.plt : { *(.rela.plt) } :text
  .rela.got : { *(.rela.got) } :text
  .rela.data : { *(.rela.data) *(.rela.data*) } :text
  /* PE SizeOfImage must be aligned to SectionAlignment (0x1000). */
  . = ALIGN(4096);
  _edata = .;
  _data_size = . - _data;

  . = ALIGN(4096);
  .dynsym   : { *(.dynsym) } :text
  . = ALIGN(4096);
  .dynstr   : { *(.dynstr) } :text
  . = ALIGN(4096);
  .note.gnu.build-id : { *(.note.gnu.build-id) } :text
  /DISCARD/ :
  {
    *(.rel.reloc)
    *(.eh_frame)
    *(.note.GNU-stack)
  }
  .comment 0 : { *(.comment) }
}
