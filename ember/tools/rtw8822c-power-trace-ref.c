/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: host environment for unchanged pinned MAC power setup functions, AI-assisted. */
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include "rtw8822c-power-model.h"
typedef uint8_t u8;
typedef uint32_t u32;
#define BIT(n) (1U << (n))
#define GENMASK(h,l) ((~0U << (l)) & (~0U >> (31-(h))))
#include "defs.h"
enum { RTW_HCI_TYPE_PCIE, RTW_HCI_TYPE_USB, RTW_HCI_TYPE_SDIO };
struct rtw_chip_info { u8 sys_func_en; };
struct rtw_dev { const struct rtw_chip_info *chip; };
/* Pinned sdio.h; the selected PCIe branch cannot access these. */
#define REG_SDIO_HSUS_CTRL 0x10250086
#define BIT_HCI_SUS_REQ BIT(0)
#define BIT_HCI_RESUME_RDY BIT(1)
#define RTW_PWR_POLLING_CNT 1
#define rtw_err(...) abort()
static int rtw_chip_wcpu_8051(struct rtw_dev *d) { (void)d; return 0; }
static int rtw_hci_type(struct rtw_dev *d) { (void)d; return RTW_HCI_TYPE_PCIE; }
static int rtw_sdio_is_sdio30_supported(struct rtw_dev *d) { (void)d; abort(); }
static void usleep_range(unsigned a, unsigned b) { (void)a; (void)b; abort(); }
static u8 rtw_read8(struct rtw_dev *d, u32 a) { (void)d; return (u8)model_read(a,1); }
static u32 rtw_read32(struct rtw_dev *d, u32 a) { (void)d; return model_read(a,4); }
static void rtw_write8(struct rtw_dev *d, u32 a, u8 v) { (void)d; model_write(a,1,v); }
static void rtw_write32(struct rtw_dev *d, u32 a, u32 v) { (void)d; model_write(a,4,v); }
static void rtw_write8_set(struct rtw_dev *d,u32 a,u8 v) { rtw_write8(d,a,rtw_read8(d,a)|v); }
static void rtw_write8_clr(struct rtw_dev *d,u32 a,u8 v) { rtw_write8(d,a,rtw_read8(d,a)&(u8)~v); }
static void rtw_write32_set(struct rtw_dev *d,u32 a,u32 v) { rtw_write32(d,a,rtw_read32(d,a)|v); }
#include "body.c"
int main(void)
{
	const struct rtw_chip_info chip = { .sys_func_en = REF_SYS_FUNC_EN };
	struct rtw_dev d = { .chip = &chip };
	unsigned stage, seed, flash, i;

	puts("# FreeBSD a143197801c680b958ab662c88cf086668a1958b unchanged MAC power setup");
	for (stage=0; stage<2; stage++) for(seed=0; seed<3; seed++) for(flash=0; flash<2; flash++) {
		model_reset(seed,flash);
		if ((stage == 0 ? rtw_mac_pre_system_cfg(&d) : __rtw_mac_init_system_cfg(&d)) != 0)
			abort();
		printf("stage %u seed %u flash %u events %u\n",stage,seed,flash,event_count);
		for(i=0;i<event_count;i++) puts(events[i]);
	}
	return 0;
}
