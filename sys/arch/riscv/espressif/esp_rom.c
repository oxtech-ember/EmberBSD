/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The mask ROM of the ESP32-S31.  The vendor's radio libraries call into it
 * at fixed addresses, the ROM keeps its own data at the top of the internal
 * RAM, and the libraries reach the registers of the radio by address, so
 * all of it is mapped where it physically is.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <uvm/uvm.h>

#include <riscv/espressif/esp_rom.h>
#include <riscv/espressif/esp_trng.h>
#include <riscv/espressif/esp_wifi_var.h>

#include "opt_espwifi.h"
#include "opt_esptrng.h"

#ifdef RISCV_LOW_IDMAP

#define	S31_IDMAP_BASE	0x20000000
#define	S31_IDMAP_SIZE	0x10000000

#define	S31_ROM_ECO_VERSION	0x2f800014
#define	S31_ROM_CRC32_LE	0x2f800784
#define	S31_ROM_OPS_TABLE_PTR	0x2f07fff4

typedef uint32_t (*rom_crc32_t)(uint32_t, const uint8_t *, uint32_t);

void
esp_rom_init(void)
{
	static const uint8_t check[] = "123456789";

	/* The peripherals, the internal RAM and the ROM, all at once. */
	pmap_md_idmap(S31_IDMAP_BASE, S31_IDMAP_SIZE);

	const uint32_t crc = ((rom_crc32_t)S31_ROM_CRC32_LE)(0, check, 9);

	aprint_normal("esprom: eco %u, ops table %#x, crc32 %#x%s\n",
	    *(volatile uint32_t *)S31_ROM_ECO_VERSION,
	    *(volatile uint32_t *)S31_ROM_OPS_TABLE_PTR, crc,
	    crc == 0xcbf43926 ? "" : " (wrong)");
#ifdef ESPTRNG
	if (!esp_trng_seed())
		return;
#endif
#ifdef ESPWIFI
	espwifi_attach();
#endif
}

#else

void
esp_rom_init(void)
{
}

#endif
