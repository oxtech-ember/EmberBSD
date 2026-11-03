/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD RTL8723D contract tests, AI-assisted. */
#ifndef RTW88_SDIO_RX_FAULT_FIXTURE_H
#define RTW88_SDIO_RX_FAULT_FIXTURE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <dev/sdmmc/rtw88_sdio_io.h>
#include <dev/ic/rtw88_hci.h>
#define RTW88_SDIO_RX_BATCH_MAX 16384U
struct rtw88_async_epoch { uint32_t generation; uint64_t epoch,key_epoch; };
struct rtw88_rx_caps { unsigned rx_desc_size; };
struct rtw88_chip_ops { struct rtw88_rx_caps caps; int (*rx_desc)(void); };
struct rtw88_softc { const struct rtw88_chip_ops *sc_chip; uint32_t sc_generation; bool sc_resources_ready,sc_powered; uint64_t sc_rx_sequence; };
struct rtw88_sdio_rx_record { size_t payload_offset,length,descriptor_offset; };
struct rtw88_sdio_rx_batch { struct rtw88_async_epoch id; unsigned count; struct rtw88_sdio_rx_record records[1]; };
struct rtw88_sdio_rx_context { struct rtw88_sdio_bus *bus; uint8_t *packet,*scratch; size_t packet_cap,scratch_cap; struct rtw88_sdio_rx_batch *batch; uint8_t counter; };
int rtw88_executor_owned(struct rtw88_softc *);
int rtw88_rx_epoch_check(struct rtw88_softc *,const struct rtw88_async_epoch *);
int rtw88_rx_deliver_epoch(struct rtw88_softc *,const uint8_t *,size_t,uint64_t,const struct rtw88_async_epoch *);
int rtw88_sdio_rx_validate(struct rtw88_softc *,const uint8_t *,size_t,struct rtw88_sdio_rx_batch *);
int rtw88_sdio_fifo_read(struct rtw88_sdio_bus *,uint8_t *,void *,size_t,uint8_t *,size_t);
int rtw88_sdio_rx_drain(struct rtw88_sdio_rx_context *,struct rtw88_softc *,size_t,uint32_t,uint64_t *);
#endif
