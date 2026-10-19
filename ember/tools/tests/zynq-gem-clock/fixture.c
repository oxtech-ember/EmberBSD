/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
typedef unsigned int u_int;
typedef unsigned int bus_addr_t;
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h,l) ((UINT32_MAX >> (31-(h))) & (UINT32_MAX << (l)))
#define __SHIFTOUT(v,m) (((v) & (m)) >> __builtin_ctz(m))
#define __SHIFTIN(v,m) (((uint32_t)(v) << __builtin_ctz(m)) & (m))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
struct clk { unsigned id; };
enum { clkid_gem0, clkid_gem1, clkid_other };
struct syscon {
    uint32_t words[256];
    bool held, deny;
    unsigned writes;
};
struct zynq7000_clkc_softc {
    struct syscon *sc_syscon;
    u_int sc_ps_clk_frequency;
    struct clk sc_clk[3];
};
static void syscon_lock(struct syscon *s) { assert(!s->held); s->held = true; }
static void syscon_unlock(struct syscon *s) { assert(s->held); s->held = false; }
static uint32_t syscon_read_4(struct syscon *s, unsigned r)
{ assert(s->held && r < sizeof(s->words) && r % 4 == 0); return s->words[r/4]; }
static void syscon_write_4(struct syscon *s, unsigned r, uint32_t v)
{
    assert(s->held && r < sizeof(s->words) && r % 4 == 0);
    s->writes++;
    if (r == 8) { assert(v == 0xdf0d); s->words[3] = 0; }
    else if (r == 4) { assert(v == 0x767b); s->words[3] = 1; }
    else { assert((r == 0x140 || r == 0x144) && s->words[3] == 0);
        if (!s->deny) s->words[r/4] = v;
    }
}
#define IFM_10_T 1
#define IFM_100_TX 2
#define IFM_1000_T 3
#define IFM_NONE 0
#define IFM_ETHER 0x100
#define IFM_FDX 0x200
#define IFM_ACTIVE 1
#define IFM_SUBTYPE(m) ((m) & 0xff)
#define CEMAC_FLAG_GEM 1
#define ISSET(v,b) ((v) & (b))
#define ETH_CFG 4
#define ETH_CFG_FD 2
#define ETH_CFG_SPD 1
#define GEM_CFG_GEN 0x400
#define LINK_STATE_DOWN 0
struct ifnet { void *if_softc; int link; };
struct mii_data { int mii_media_active, mii_media_status; };
struct cemac_softc {
    struct mii_data sc_mii;
    unsigned cemac_flags;
    int (*sc_set_ref_clock)(struct cemac_softc *, u_int);
    void *sc_dev;
};
static unsigned cfg, cfgwrites, clockcalls, requested, errors;
static int clockerror;
#define CEMAC_READ(r) (assert((r) == ETH_CFG), cfg)
#define CEMAC_WRITE(r,v) do { assert((r) == ETH_CFG); cfg = (v); cfgwrites++; } while (0)
#define aprint_error_dev(...) (++errors)
static void if_link_state_change(struct ifnet *ifp, int state) { ifp->link = state; }
static int fake_clock(struct cemac_softc *sc, u_int rate)
{ (void)sc; clockcalls++; requested = rate; return clockerror; }
/* ACTUAL_DRIVER */
static void test_clock(void)
{
    struct syscon s = {0};
    struct zynq7000_clkc_softc c = {.sc_syscon=&s, .sc_ps_clk_frequency=33333333};
    const unsigned rates[] = {125000000, 25000000, 2500000, 25000000, 125000000};
    s.words[IO_PLL_CTRL/4] = 30u << 12;
    s.words[ARM_PLL_CTRL/4] = 40u << 12;
    s.words[DDR_PLL_CTRL/4] = 32u << 12;
    for (unsigned gem=0; gem<2; gem++) {
        const unsigned reg = gem ? GEM1_CLK_CTRL : GEM0_CLK_CTRL;
        for (unsigned locked=0; locked<2; locked++) {
            s.words[SLCR_LOCKSTA/4] = locked;
            s.words[reg/4] = 0x80000001u | (8 << 8) | (1 << 20);
            for (unsigned i=0; i<sizeof(rates)/sizeof(rates[0]); i++) {
                unsigned other = s.words[(gem ? GEM0_CLK_CTRL : GEM1_CLK_CTRL)/4];
                assert(zynq7000_clkc_clk_set_rate(&c, &c.sc_clk[gem], rates[i]) == 0);
                unsigned actual=zynq7000_clkc_get_rate_gem(&c, reg);
                assert(actual <= rates[i] && rates[i] - actual <= 2);
                assert((s.words[reg/4] & ~(GEM_CLK_DIV0|GEM_CLK_DIV1)) == 0x80000001u);
                assert(s.words[SLCR_LOCKSTA/4] == locked && !s.held);
                assert(s.words[(gem ? GEM0_CLK_CTRL : GEM1_CLK_CTRL)/4] == other);
                unsigned writes=s.writes;
                assert(zynq7000_clkc_clk_set_rate(&c, &c.sc_clk[gem], rates[i]) == 0);
                assert(s.writes == writes);
            }
        }
    }
    unsigned writes=s.writes;
    assert(zynq7000_clkc_clk_set_rate(&c,&c.sc_clk[2],25000000)==ENXIO);
    assert(zynq7000_clkc_clk_set_rate(&c,&c.sc_clk[0],0)==EINVAL);
    assert(zynq7000_clkc_clk_set_rate(&c,&c.sc_clk[0],UINT_MAX)==EINVAL);
    assert(s.writes==writes);
    s.words[GEM0_CLK_CTRL/4] |= GEM_CLK_EMIO;
    assert(zynq7000_clkc_get_rate_gem(&c,GEM0_CLK_CTRL)==0);
    assert(zynq7000_clkc_clk_set_rate(&c,&c.sc_clk[0],25000000)==EINVAL);
    assert(s.writes==writes);
    s.words[GEM0_CLK_CTRL/4] = 1 | (2 << 4) | (8 << 8) | (1 << 20);
    assert(zynq7000_clkc_get_rate_gem(&c,GEM0_CLK_CTRL)==166666665);
    s.words[GEM0_CLK_CTRL/4] = 1 | (3 << 4) | (8 << 8) | (1 << 20);
    assert(zynq7000_clkc_get_rate_gem(&c,GEM0_CLK_CTRL)==133333332);
    assert(zynq7000_clkc_clk_set_rate(&c,&c.sc_clk[0],25000000)==ERANGE);
    assert(s.writes==writes);
    s.words[GEM0_CLK_CTRL/4] = 1;
    assert(zynq7000_clkc_get_rate_gem(&c,GEM0_CLK_CTRL)==0);
    s.words[IO_PLL_CTRL/4] |= 1;
    assert(zynq7000_clkc_clk_set_rate(&c,&c.sc_clk[0],25000000)==EINVAL);
    s.words[IO_PLL_CTRL/4] = 1 << 4;
    s.words[GEM0_CLK_CTRL/4] = 1 | (1 << 8) | (1 << 20);
    assert(zynq7000_clkc_get_rate_gem(&c,GEM0_CLK_CTRL)==33333333);
    s.words[IO_PLL_CTRL/4] = 30u << 12;
    s.words[SLCR_LOCKSTA/4] = 1;
    s.deny=true;
    assert(zynq7000_clkc_clk_set_rate(&c,&c.sc_clk[0],25000000)==EIO);
    assert(s.words[SLCR_LOCKSTA/4]==1 && !s.held);
}
static void test_mac(void)
{
    struct cemac_softc c = {.cemac_flags=CEMAC_FLAG_GEM,.sc_set_ref_clock=fake_clock};
    struct ifnet ifp = {.if_softc=&c,.link=1};
    const unsigned rates[] = {2500000,25000000,125000000};
    const unsigned masks[] = {0,ETH_CFG_SPD,ETH_CFG_SPD|GEM_CFG_GEN};
    for (unsigned i=0;i<3;i++) {
        c.sc_mii.mii_media_active=IFM_ETHER|IFM_FDX|(i+1);
        c.sc_mii.mii_media_status=IFM_ACTIVE;
        cfg=0x80000403u;
        cemac_statchg(&ifp);
        assert(requested==rates[i] && clockcalls==i+1);
        assert(cfg==(0x80000000u|ETH_CFG_FD|masks[i]));
    }
    c.sc_mii.mii_media_status=0;
    cemac_statchg(&ifp);
    assert(clockcalls==3);
    c.sc_mii.mii_media_status=IFM_ACTIVE;
    clockerror=EIO;
    unsigned writes=cfgwrites;
    cemac_statchg(&ifp);
    assert(errors==1 && ifp.link==LINK_STATE_DOWN && cfgwrites==writes);
    assert(!(c.sc_mii.mii_media_status&IFM_ACTIVE));
    c.sc_set_ref_clock=NULL;
    c.sc_mii.mii_media_active=IFM_ETHER|IFM_100_TX;
    cemac_statchg(&ifp);
    assert(!(cfg&ETH_CFG_FD) && (cfg&ETH_CFG_SPD));
}
int main(void) { test_clock(); test_mac(); puts("Zynq GEM clock and MII contracts passed"); return 0; }
