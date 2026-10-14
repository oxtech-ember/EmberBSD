/*	$NetBSD: elf_aarch64_efi.lds,v 1.3 2020/04/26 06:46:46 rin Exp $	*/

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)
/* EmberBSD: binutils >= 2.39 rejects program headers outside every
 * PT_LOAD. Force the headers into one LOAD segment and assign all
 * sections to it, matching what gnu-efi upstream did for new ld. */
PHDRS
{
  text PT_LOAD FILEHDR PHDRS;
}
SECTIONS
{
  .text 0x1000 : {
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
  . = ALIGN(512);
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
