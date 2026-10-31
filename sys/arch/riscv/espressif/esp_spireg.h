/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Register subset from ESP-IDF v6.1, fff9895c82d744c7237be8847347bdd1b07c6643,
 * components/soc/esp32s31/register/soc/{spi,hp_sys_clkrst}_reg.h.
 */
#ifndef _RISCV_ESPRESSIF_ESP_SPIREG_H_
#define _RISCV_ESPRESSIF_ESP_SPIREG_H_

#define ESPSPI_CMD		0x00
#define ESPSPI_UPDATE		__BIT(23)
#define ESPSPI_USR		__BIT(24)
#define ESPSPI_CTRL		0x08
#define ESPSPI_CLOCK		0x0c
#define ESPSPI_CLOCK_DIRECT	__BIT(31)
#define ESPSPI_USER		0x10
#define ESPSPI_DOUTDIN		__BIT(0)
#define ESPSPI_CK_OUT_EDGE	__BIT(9)
#define ESPSPI_FWRITE_DUAL	__BIT(12)
#define ESPSPI_FWRITE_QUAD	__BIT(13)
#define ESPSPI_USR_MOSI		__BIT(27)
#define ESPSPI_USR_MISO		__BIT(28)
#define ESPSPI_USER1		0x14
#define ESPSPI_USER2		0x18
#define ESPSPI_DLEN		0x1c
#define ESPSPI_MISC		0x20
#define ESPSPI_CS_DISABLE	__BITS(5, 0)
#define ESPSPI_CK_IDLE_EDGE	__BIT(29)
#define ESPSPI_CS_KEEP_ACTIVE	__BIT(30)
#define ESPSPI_DMA_CONF		0x30
#define ESPSPI_DMA_BASE		(__BIT(19) | __BIT(20))
#define ESPSPI_RX_FIFO_RESET	__BIT(29)
#define ESPSPI_TX_FIFO_RESET	__BIT(30)
#define ESPSPI_INT_ENA		0x34
#define ESPSPI_INT_CLR		0x38
#define ESPSPI_INT_RAW		0x3c
#define ESPSPI_TRANS_DONE	__BIT(12)
#define ESPSPI_W0		0x98
#define ESPSPI_SLAVE		0xe0
#define ESPSPI_CLK_GATE		0xe8
#define ESPSPI_CLK_ENABLE	__BITS(2, 0)
#define ESPSPI_REG_SIZE		0xec
#define ESPSPI_FIFO_SIZE		64

#define ESPSPI2_CLKRST		0x6c
#define ESPSPI3_CLKRST		0x70
#define ESPSPI_CLKRST_RESET	__BIT(2)
#define ESPSPI_CLKRST_DIVSEL	(__BITS(22, 7) | __BITS(5, 4))
#define ESPSPI_CLKRST_ENABLE	(__BIT(0) | __BIT(1) | __BIT(6) | __BIT(23))

#endif
