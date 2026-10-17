/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: host environment for unchanged pinned BSD packet-H2C producers. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint32_t __le32;
typedef uint16_t __le16;
#define BIT(n) (1U << (n))
#define GENMASK(h,l) ((~0U << (l)) & (~0U >> (31-(h))))
#define lockdep_assert_held(x) ((void)0)
#define rtw_err(...) abort()
struct rtw_fifo_conf { u16 rsvd_fw_txbuf_addr, rsvd_boundary; };
struct rtw_hal { u8 rf_type, cut_version, antenna_tx, antenna_rx; };
struct rtw_efuse { u8 rfe_option; };
struct rtw_dev {
	struct rtw_fifo_conf fifo;
	struct rtw_hal hal;
	struct rtw_efuse efuse;
	struct { u16 seq; } h2c;
};
enum { RF_1T1R = 0, RF_2T2R = 2 };
static void
le32p_replace_bits(__le32 *p, u32 value, u32 mask)
{
	u8 *b = (u8 *)p;
	u32 old = (u32)b[0] | (u32)b[1]<<8 | (u32)b[2]<<16 | (u32)b[3]<<24;
	u32 v = (old & ~mask) | ((value << __builtin_ctz(mask)) & mask);
	unsigned i;
	for(i=0;i<4;i++) b[i]=(u8)(v>>(8*i));
}
static int rtw_chip_wcpu_8051(struct rtw_dev*d) { (void)d; return 0; }
static int
rtw_hci_write_data_h2c(struct rtw_dev*d,u8 *p,u32 size)
{
	unsigned i;
	(void)d;
	if(size!=32) abort();
	for(i=0;i<size;i++) printf("%02x",p[i]);
	putchar('\n');
	return 0;
}
#include "defs.h"
#include "body.c"
int main(void)
{
	struct rtw_pci_tx_buffer_desc bd[2] = {0};
	unsigned r,c;
	puts("# FreeBSD a143197801c680b958ab662c88cf086668a1958b unchanged packet producers");
	printf("layout %zu %zu %zu %zu\n",sizeof(bd[0]),
	    offsetof(struct rtw_pci_tx_buffer_desc,buf_size),
	    offsetof(struct rtw_pci_tx_buffer_desc,psb_len),
	    offsetof(struct rtw_pci_tx_buffer_desc,dma));
	/* These source assignments use the actual extracted PCI descriptor type. */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define LE16(v) ((uint16_t)(v))
#define LE32(v) ((uint32_t)(v))
#else
#define LE16(v) __builtin_bswap16((uint16_t)(v))
#define LE32(v) __builtin_bswap32((uint32_t)(v))
#endif
	bd[0].buf_size=LE16(48);
	bd[0].psb_len=LE16(1);
	bd[0].dma=LE32(0x300000);
	bd[1].buf_size=LE16(32);
	bd[1].dma=LE32(0x300030);
	printf("bd ");
	for(r=0;r<sizeof(bd);r++) printf("%02x",((const u8 *)bd)[r]);
	putchar('\n');
	for(r=0;r<7;r++) for(c=0;c<7;c++) {
		struct rtw_dev d={ .fifo={1994,1938}, .hal={RF_2T2R,(u8)c,3,3},
		    .efuse={(u8)r} };
		printf("case %u %u\n",r,c);
		rtw_fw_send_general_info(&d);
		rtw_fw_send_phydm_info(&d);
	}
	return 0;
}
