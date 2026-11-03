/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
#ifndef CORE_PHY_ORDER_FIXTURE_H
#define CORE_PHY_ORDER_FIXTURE_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#define RTW8723D_POWER_RATES 16
#define RTW8723D_PHY_SDIO 3
struct rtw88_softc { unsigned sc_stop_requested,sc_generation,sc_powered,sc_resources_ready,sc_started,sc_running,sc_rf_enabled,sc_cut,sc_rfe,sc_xtal; const void *sc_ops; void *sc_bus; };
struct rtw8723d_boot_ops { bool (*cancelled)(void *); void (*wait_ms)(void *,unsigned); };
struct rtw8723d_core_hooks { bool (*revoked)(void *); void *cookie; };
struct rtw8723d_core_boot { struct rtw88_softc *core; struct rtw8723d_boot_ops ops; struct rtw8723d_core_hooks hooks; uint32_t generation; bool owned,firmware_booted,failed; };
struct rtw8723d_phy_condition { unsigned interface,cut,rfe; };
struct rtw8723d_efuse { uint8_t rfe,xtal,afe; };
struct rtw88_profile { int x; }; struct rtw8723d_power_policy { int x; };
struct rtw8723d_iqk_matrix { int x; };
struct rtw8723d_core_phy_config { struct rtw8723d_phy_condition condition; struct rtw8723d_efuse efuse; struct rtw88_profile profile; struct rtw8723d_power_policy power; unsigned channel; };
struct rtw8723d_core_phy_result { struct rtw8723d_iqk_matrix iqk; uint32_t generation; unsigned channel; bool configured; };
struct rtw88_io { int error; };
static inline void rtw88_io_init(struct rtw88_io *i,const void *o,void *c) { (void)o;(void)c;i->error=0; }
int rtw8723d_core_power_off(struct rtw8723d_core_boot *);
int rtw8723d_core_phy_configure(struct rtw8723d_core_boot *,const struct rtw8723d_core_phy_config *,struct rtw8723d_core_phy_result *);
int rtw8723d_power_compute(const struct rtw8723d_efuse *,const struct rtw88_profile *,const struct rtw8723d_power_policy *,unsigned,uint8_t *);
int rtw8723d_mac_configure(struct rtw88_io *,int(*)(void *),void *);
int rtw8723d_swcrypto_configure(struct rtw88_io *,int(*)(void *),void *);
int rtw8723d_phy_configure(struct rtw88_io *,const struct rtw8723d_phy_condition *,uint8_t,uint8_t,int(*)(void *),void *);
int rtw8723d_dm_before_lck(struct rtw88_io *,int(*)(void *),void *);
int rtw8723d_dm_after_lck(struct rtw88_io *,int(*)(void *),void *);
int rtw8723d_lck(struct rtw88_io *,const struct rtw8723d_boot_ops *);
int rtw8723d_iqk_calibrate(struct rtw88_io *,const struct rtw8723d_boot_ops *,struct rtw8723d_iqk_matrix *);
int rtw8723d_channel20(struct rtw88_io *,const struct rtw8723d_boot_ops *,unsigned);
int rtw8723d_power_program(struct rtw88_io *,const struct rtw8723d_efuse *,const struct rtw88_profile *,const struct rtw8723d_power_policy *,unsigned,int(*)(void *),void *);
#endif
