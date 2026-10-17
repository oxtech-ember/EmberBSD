/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: host environment for unchanged pinned cold ADCK/DACK functions. */
#include <stdbool.h>
#include <errno.h>
#include "rtw8822c-dack-model.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define BIT(n) (1U<<(n))
#define GENMASK(h,l) ((~0U<<(l))&(~0U>>(31-(h))))
#define swap(a,b) do { __typeof__(a) t=(a); (a)=(b); (b)=t; } while(0)
static int warn_on(int x) { if(x) abort(); return x; }
#define WARN_ON(x) warn_on(x)
#define rtw_dbg(dev,kind,...) do { if(0) fprintf(stderr,__VA_ARGS__); } while(0)
#define rtw_err(dev,...) do { model.errors++; fprintf(stderr,__VA_ARGS__); } while(0)
#define RTW_DBG_RFK 0
#define RFREG_MASK 0xfffff
#define DACK_MSBK_BACKUP_NUM 15
#define DACK_DCK_BACKUP_NUM 2
enum rtw_rf_path { RF_PATH_A, RF_PATH_B };
#include "defs.h"
struct rtw_dm_info {
	u32 dack_adck[2];
	u16 dack_msbk[2][2][15];
	u8 dack_dck[2][2][2];
};
struct rtw_dev;
struct rtw_chip_ops { u32 (*read_rf)(struct rtw_dev *,enum rtw_rf_path,u32,u32); };
struct rtw_chip_info { u32 rf_base_addr[2],rf_sipi_addr[2]; const struct rtw_chip_ops *ops; };
struct rtw_hal { unsigned rf_phy_num; };
struct rtw_dev { struct rtw_dm_info dm_info; struct rtw_hal hal; const struct rtw_chip_info *chip; };
static u32 rtw_phy_read_rf(struct rtw_dev *,enum rtw_rf_path,u32,u32);
static bool rtw_phy_write_rf_reg_mix(struct rtw_dev *,enum rtw_rf_path,u32,u32,u32);
static u32 rtw_read32(struct rtw_dev*d,u32 a) { (void)d; return model_read(a,4); }
static void rtw_write8(struct rtw_dev*d,u32 a,u8 v) { (void)d; model_write(a,1,v); }
static void rtw_write16(struct rtw_dev*d,u32 a,u16 v) { (void)d; model_write(a,2,v); }
static void rtw_write32(struct rtw_dev*d,u32 a,u32 v) { (void)d; model_write(a,4,v); }
static u32 rtw_read32_mask(struct rtw_dev*d,u32 a,u32 mask) { return (rtw_read32(d,a)&mask)>>__builtin_ctz(mask); }
static void rtw_write32_mask(struct rtw_dev*d,u32 a,u32 mask,u32 v) { rtw_write32(d,a,(rtw_read32(d,a)&~mask)|((v<<__builtin_ctz(mask))&mask)); }
static void rtw_write32_set(struct rtw_dev*d,u32 a,u32 v) { rtw_write32(d,a,rtw_read32(d,a)|v); }
static void rtw_write32_clr(struct rtw_dev*d,u32 a,u32 v) { rtw_write32(d,a,rtw_read32(d,a)&~v); }
static u32 rtw_read_rf(struct rtw_dev*d,u8 p,u32 a,u32 mask) { return rtw_phy_read_rf(d,(enum rtw_rf_path)p,a,mask); }
static void
rtw_write_rf(struct rtw_dev*d,u8 p,u32 a,u32 mask,u32 v)
{
	if(!rtw_phy_write_rf_reg_mix(d,(enum rtw_rf_path)p,a,mask,v)) abort();
	/* Final temporary RF restore ends the cold calibration trace. */
	if(a==0x8f && mask==RFREG_MASK && ++model.restored==2) model.cold_count=model.count;
}
static void udelay(unsigned us) { model_delay(us); }
static void mdelay(unsigned ms) { model_delay(ms*1000); }
/* util.c uses int for its loop over an unsigned register count. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "body.c"
#pragma GCC diagnostic pop
int main(void)
{
	const struct rtw_chip_ops ops={rtw_phy_read_rf};
	const struct rtw_chip_info chip={{0x3c00,0x4c00},{0x1808,0x4108},&ops};
	unsigned fixture,i;
	puts("# FreeBSD a143197801c680b958ab662c88cf086668a1958b unchanged cold ADCK/DACK");
	for(fixture=0;fixture<6;fixture++) {
		struct rtw_dev d={.hal={2},.chip=&chip};
		model_reset(fixture);
		rtw8822c_rf_dac_cal(&d);
		if(model.errors || !model.cold_count) abort();
		printf("case %u events %u cold %u\n",fixture,model.count,model.cold_count);
		for(i=0;i<model.count;i++) printf("%u %04x %08x\n",model.events[i].kind,model.events[i].address,model.events[i].value);
	}
	return 0;
}
