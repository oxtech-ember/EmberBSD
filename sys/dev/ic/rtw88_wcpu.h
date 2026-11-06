/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_IC_RTW88_WCPU_H_
#define _DEV_IC_RTW88_WCPU_H_
#include <dev/ic/rtw88var.h>
int rtw88_wcpu_fw_download(struct rtw88_softc *, uint32_t, const uint8_t *, size_t, const struct rtw88_fw_info *);
int rtw88_wcpu_hw_feature_request(struct rtw88_softc *, uint32_t);
int rtw88_wcpu_hw_feature_read(struct rtw88_softc *, uint32_t);
int rtw88_wcpu_edca(struct rtw88_softc *, const struct rtw88_edca_params [RTW88_WME_NAC], unsigned);
int rtw88_wcpu_mailbox_begin(struct rtw88_softc *, unsigned, int *);
int rtw88_wcpu_mailbox_end(struct rtw88_softc *, int);
int rtw88_wcpu_command(struct rtw88_softc *, uint8_t, const void *, size_t);
int rtw88_wcpu_station(struct rtw88_softc *, const uint8_t *, unsigned, unsigned, unsigned, int);
int rtw88_wcpu_ra(struct rtw88_softc *, const struct rtw88_ra_config *);
/* Caller serializes LTE indirect operations; value is already mask-aligned. */
int rtw88_wcpu_lte_read(struct rtw88_softc *, uint32_t, uint16_t, uint32_t *);
int rtw88_wcpu_lte_write(struct rtw88_softc *, uint32_t, uint16_t, uint32_t);
int rtw88_wcpu_lte_update(struct rtw88_softc *, uint32_t, uint16_t, uint32_t, uint32_t);
#endif
