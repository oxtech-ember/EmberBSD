/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: host environment for unchanged pinned BSD PCI packet-H2C producers. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint32_t __le32;
typedef uint16_t __le16;
typedef uint64_t dma_addr_t;
#define __packed __attribute__((packed))
#define BIT(n) (1U << (n))
#define GENMASK(h,l) ((~0U << (l)) & (~0U >> (31-(h))))
#define lockdep_assert_held(x) ((void)0)
#define rtw_err(...) abort()
#define spin_lock_bh(x) ((void)0)
#define spin_unlock_bh(x) ((void)0)
#define set_bit(n,p) ((void)0)
#define DMA_TO_DEVICE 1
#define RTK_PCI_TXBD_OWN_OFFSET 15
#define DESC_RATE24M 8
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define cpu_to_le16(v) ((uint16_t)(v))
#define cpu_to_le32(v) ((uint32_t)(v))
#else
#define cpu_to_le16(v) __builtin_bswap16((uint16_t)(v))
#define cpu_to_le32(v) __builtin_bswap32((uint32_t)(v))
#endif
static __le32
le32_encode_bits(u32 value, u32 mask)
{
	return cpu_to_le32((value << __builtin_ctz(mask)) & mask);
}
static void
le32p_replace_bits(__le32 *p, u32 value, u32 mask)
{
	u8 *b = (u8 *)p;
	u32 old = (u32)b[0] | (u32)b[1]<<8 | (u32)b[2]<<16 | (u32)b[3]<<24;
	u32 v = (old & ~mask) | ((value << __builtin_ctz(mask)) & mask);
	unsigned i;
	for(i=0;i<4;i++) b[i]=(u8)(v>>(8*i));
}
#include "defs.h"
/* Model allocation, DMA mapping, locks, queue storage and the doorbell only.
 * The extracted producers below build the packet, TXDESC and BD themselves. */
struct sk_buff {
	u8 storage[80], *data;
	u32 len, priority;
	struct rtw_pci_tx_data tx_data;
};
struct rtw_pci_tx_ring {
	struct { u8 *head; u32 wp,rp,len; } r;
	struct sk_buff *queue;
};
struct fake_pci_device { int dev; };
struct rtw_pci {
	struct rtw_pci_tx_ring tx_rings[RTK_MAX_TX_QUEUE_NUM];
	struct fake_pci_device *pdev;
};
struct rtw_chip_info { u32 tx_pkt_desc_sz, tx_buf_desc_sz; bool old_datarate_fb_limit; };
struct rtw_fifo_conf { u16 rsvd_fw_txbuf_addr, rsvd_boundary; };
struct rtw_hal { u8 rf_type, cut_version, antenna_tx, antenna_rx; };
struct rtw_efuse { u8 rfe_option; };
struct rtw_dev {
	struct rtw_fifo_conf fifo;
	struct rtw_hal hal;
	struct rtw_efuse efuse;
	struct { u16 seq; } h2c;
	const struct rtw_chip_info *chip;
	struct rtw_pci *priv;
};
enum { RF_1T1R = 0, RF_2T2R = 2 };
static struct sk_buff *
dev_alloc_skb(u32 length)
{
	struct sk_buff *skb;
	if(length!=80) abort();
	skb=calloc(1,sizeof(*skb));
	if(!skb) abort();
	skb->data=skb->storage;
	return skb;
}
static void skb_reserve(struct sk_buff *skb,u32 n) { if(n!=48) abort(); skb->data+=n; }
static void
skb_put_data(struct sk_buff *skb,const u8 *p,u32 n)
{
	if(skb->data!=skb->storage+48 || n!=32 || skb->len!=0) abort();
	memcpy(skb->data,p,n); skb->len=n;
}
static u8 *
skb_push(struct sk_buff *skb,u32 n)
{
	if(skb->data!=skb->storage+48 || n!=48 || skb->len!=32) abort();
	skb->data-=n; skb->len+=n; return skb->data;
}
static dma_addr_t
dma_map_single(void *device,void *data,u32 size,int direction)
{
	(void)device; (void)data;
	if(size!=80 || direction!=DMA_TO_DEVICE) abort();
	return 0x300000;
}
static bool dma_mapping_error(void *device,dma_addr_t dma) { (void)device; return dma!=0x300000; }
static struct rtw_pci_tx_data *rtw_pci_get_tx_data(struct sk_buff *skb) { return &skb->tx_data; }
static void skb_queue_tail(struct sk_buff **queue,struct sk_buff *skb) { if(*queue) abort(); *queue=skb; }
static void rtw_pci_release_rsvd_page(struct rtw_pci *pci,struct rtw_pci_tx_ring *ring) { (void)pci; (void)ring; abort(); }
static int rtw_chip_wcpu_8051(struct rtw_dev*d) { (void)d; return 0; }
static int rtw_pci_write_data_h2c(struct rtw_dev *,u8 *,u32);
static int rtw_hci_write_data_h2c(struct rtw_dev*d,u8 *p,u32 size) { return rtw_pci_write_data_h2c(d,p,size); }
static u8 expected_desc[48], expected_bd[16];
static bool emitting;
static void
hex(const char *prefix,const u8 *p,size_t n)
{
	size_t i;
	fputs(prefix,stdout);
	for(i=0;i<n;i++) printf("%02x",p[i]);
	putchar('\n');
}
static void
rtw_pci_tx_kick_off_queue(struct rtw_dev *d,enum rtw_tx_queue_type queue)
{
	struct rtw_pci_tx_ring *ring=&d->priv->tx_rings[queue];
	struct sk_buff *skb=ring->queue;
	const u8 *bd=ring->r.head+(ring->r.wp-1)*16;
	if(queue!=RTW_TX_QUEUE_H2C || !skb || skb->len!=80) abort();
	if(!emitting) {
		memcpy(expected_desc,skb->data,48);
		memcpy(expected_bd,bd,16);
	} else {
		if(memcmp(expected_desc,skb->data,48) || memcmp(expected_bd,bd,16)) abort();
		hex("",skb->data+48,32);
	}
	ring->queue=NULL;
	ring->r.rp=ring->r.wp;
	free(skb);
}
#include "body.c"
int main(void)
{
	const struct rtw_chip_info chip={48,16,false};
	struct fake_pci_device device={0};
	struct rtw_pci pci={ .pdev=&device };
	u8 descriptors[128*16]={0};
	struct rtw_dev d={ .fifo={1994,1938}, .hal={RF_2T2R,0,3,3},
	    .chip=&chip, .priv=&pci };
	unsigned r,c;
	pci.tx_rings[RTW_TX_QUEUE_H2C].r.head=descriptors;
	pci.tx_rings[RTW_TX_QUEUE_H2C].r.len=128;
	/* Capture one actual producer result before emitting invariant metadata. */
	rtw_fw_send_general_info(&d);
	puts("# FreeBSD a143197801c680b958ab662c88cf086668a1958b unchanged PCI packet producers");
	printf("layout %zu %zu %zu %zu\n",sizeof(struct rtw_pci_tx_buffer_desc),
	    offsetof(struct rtw_pci_tx_buffer_desc,buf_size),
	    offsetof(struct rtw_pci_tx_buffer_desc,psb_len),
	    offsetof(struct rtw_pci_tx_buffer_desc,dma));
	hex("txdesc ",expected_desc,sizeof(expected_desc));
	hex("bd ",expected_bd,sizeof(expected_bd));
	emitting=true;
	for(r=0;r<7;r++) for(c=0;c<7;c++) {
		d.h2c.seq=0;
		d.hal.cut_version=(u8)c;
		d.efuse.rfe_option=(u8)r;
		printf("case %u %u\n",r,c);
		rtw_fw_send_general_info(&d);
		rtw_fw_send_phydm_info(&d);
	}
	return 0;
}
