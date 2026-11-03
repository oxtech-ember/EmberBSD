/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; AI-assisted shared PHY table behavioral contracts. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <dev/ic/rtw88_phy_table.h>
#include <dev/ic/rtw8723d_tables.h>
#include <dev/ic/rtw8822c_table.h>

struct model { unsigned matches, writes, checks, fail_write, cancel_at, fail_match, invalid_match; uint32_t addr[8], data[8]; };
static int match(void *cookie, uint32_t condition, int *out) {
 struct model *m=cookie; m->matches++; if (m->fail_match) return EIO;
 *out=m->invalid_match ? 2 : (int)(condition&1); return 0;
}
static int write_reg(void *cookie, uint32_t addr, uint32_t value) {
 struct model *m=cookie; unsigned i=m->writes++; if (i<8) {m->addr[i]=addr; m->data[i]=value;}
 return m->fail_write==m->writes ? EIO : 0;
}
static int check(void *cookie) {
 struct model *m=cookie; m->checks++; return m->cancel_at==m->checks ? ECANCELED : 0;
}
int main(void) {
 const uint32_t words[]={0x100,0x11, 0x80000000,0, 0x40000000,0, 0x104,0x22,
  0x90000001,0, 0x40000000,0, 0x108,0x33, 0xa0000000,0, 0x10c,0x44,
  0xb0000000,0, 0x110,0x55};
 const uint32_t malformed[]={0x80000000,0,0x100,0x11};
 const uint32_t fallback[]={0x80000000,0,0x40000000,0,0x104,0x22,
  0xa0000000,0,0x10c,0x44,0xb0000000,0};
 const uint32_t first[]={0x80000001,0,0x40000000,0,0x104,0x22,
  0xa0000000,0,0x10c,0x44,0xb0000000,0};
 const struct rtw88_phy_table_ops ops={.match=match,.write=write_reg,.check=check};
 struct model m={0};
 const struct rtw8822c_table *tables[]={&rtw8822c_mac_tbl,&rtw8822c_agc_tbl,
  &rtw8822c_bb_tbl,&rtw8822c_rf_a_tbl,&rtw8822c_rf_b_tbl,&rtw8822c_array_mp_cal_init_tbl};
 const struct rtw8723d_phy_image *legacy[]={&rtw8723d_phy_mac,&rtw8723d_phy_agc,
  &rtw8723d_phy_bb,&rtw8723d_phy_rf_a};
 assert(rtw88_phy_table_apply(words,sizeof(words)/sizeof(words[0]),&ops,&m)==0);
 assert(m.matches==2 && m.writes==3 && m.addr[0]==0x100 && m.data[0]==0x11 &&
  m.addr[1]==0x108 && m.data[1]==0x33 && m.addr[2]==0x110 && m.data[2]==0x55);
 m=(struct model){0};
 assert(rtw88_phy_table_apply(fallback,sizeof(fallback)/sizeof(fallback[0]),&ops,&m)==0);
 assert(m.writes==1 && m.addr[0]==0x10c && m.data[0]==0x44);
 m=(struct model){0};
 assert(rtw88_phy_table_apply(first,sizeof(first)/sizeof(first[0]),&ops,&m)==0);
 assert(m.writes==1 && m.addr[0]==0x104 && m.data[0]==0x22);
 m=(struct model){0};
 assert(rtw88_phy_table_apply(malformed,4,&ops,&m)==EINVAL && m.checks==0 && m.writes==0);
 m=(struct model){.fail_match=1};
 assert(rtw88_phy_table_apply(words,sizeof(words)/sizeof(words[0]),&ops,&m)==EIO && m.writes==1);
 m=(struct model){.invalid_match=1};
 assert(rtw88_phy_table_apply(words,sizeof(words)/sizeof(words[0]),&ops,&m)==EINVAL && m.writes==1);
 m=(struct model){.fail_write=2};
 assert(rtw88_phy_table_apply(words,sizeof(words)/sizeof(words[0]),&ops,&m)==EIO && m.writes==2);
 m=(struct model){.cancel_at=2};
 assert(rtw88_phy_table_apply(words,sizeof(words)/sizeof(words[0]),&ops,&m)==ECANCELED && m.writes==1);
 for (unsigned i=0;i<sizeof(tables)/sizeof(tables[0]);i++)
  assert(rtw88_phy_table_validate(tables[i]->data,tables[i]->count)==0);
 for (unsigned i=0;i<sizeof(legacy)/sizeof(legacy[0]);i++)
  assert(rtw88_phy_table_validate(legacy[i]->data,legacy[i]->count)==0);
 puts("PASS: PHY condition selection, failure stops and all 8822C/8723D table structures");
 return 0;
}
