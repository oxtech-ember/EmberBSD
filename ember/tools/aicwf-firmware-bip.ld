/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted own patch layout. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(aicwf_bip_key)
PHDRS
{
  key PT_LOAD FLAGS(5);
  rx PT_LOAD FLAGS(5);
}
SECTIONS
{
  .aicwf_bip_key 0x142be0 : {
    KEEP(*(.aicwf_bip_key))
    ASSERT(. <= 0x142d34, "BIP key function exceeds its original span");
    FILL(0x00de)
    . = 340;
  } :key
  .aicwf_bip_rx 0x165e48 : {
    KEEP(*(.aicwf_bip_rx))
    ASSERT(. <= 0x165fc8, "BIP RX function exceeds its original span");
    FILL(0x00de)
    . = 384;
  } :rx
  /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.aicwf_bip_key) == 340, "BIP key output size differs")
ASSERT(SIZEOF(.aicwf_bip_rx) == 384, "BIP RX output size differs")
