/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Native bounded batch validation; AI-assisted. */
#ifdef _KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/sdmmc/rtw88_sdio_rx.h>

static int
rtw88_sdio_rx_record(struct rtw88_softc *sc, const uint8_t *data,
    size_t remaining, struct rtw88_rx_info *info, size_t *advance)
{
	size_t used, aligned;
	int error;

	if (remaining < 24)
		return EBADMSG;
	error = sc->sc_chip->rx_desc(sc, data, remaining, info);
	if (error != 0)
		return error;
	if (info->shift > 3 || info->driver_info_size > 120 ||
	    (info->driver_info_size & 7) != 0 ||
	    info->driver_info_offset != 24 + (size_t)info->shift ||
	    info->payload_offset != info->driver_info_offset + info->driver_info_size ||
	    info->packet_size == 0 || info->payload_offset > remaining ||
	    info->packet_size > remaining - info->payload_offset)
		return EBADMSG;
	if ((info->flags & RTW88_RX_C2H) != 0) {
		/* A corrupt firmware report must not reach event dispatch. */
		if ((info->flags & (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR)) != 0)
			return EBADMSG;
	} else if ((info->flags &
	    (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR)) == 0) {
		if (info->packet_size < 10)
			return EBADMSG;
		if (info->rate > 19 || info->bandwidth != 0)
			return EOPNOTSUPP;
		/* PHY observations are optional metadata. Unsupported/invalid RSSI
		 * must not discard an otherwise bounded, checksum-valid DATA frame. */
	}
	/* CRC/ICV flags describe an ordinary bad RF frame, not a malformed
	 * aggregation boundary. Preserve its bounded record; the core drops it
	 * before payload admission. Good records later in this batch survive. */
	/* All arithmetic is bounded by the validated logical remaining length. */
	used = info->payload_offset + info->packet_size;
	aligned = (used + 7) & ~(size_t)7;
	*advance = remaining <= aligned ? remaining : aligned;
	return 0;
}

int
rtw88_sdio_rx_validate(struct rtw88_softc *sc, const uint8_t *data,
    size_t length, struct rtw88_sdio_rx_batch *out)
{
	struct rtw88_rx_info info;
	size_t offset, advance;
	unsigned count, index;
	uintptr_t input, output;
	int error;

	if (out == NULL)
		return EINVAL;
	/* Refuse aliases before even invalidating a previous result. */
	input = (uintptr_t)data;
	output = (uintptr_t)out;
	if (data != NULL && (input <= output ? output - input < length :
	    input - output < sizeof(*out)))
		return EINVAL;
	out->count = 0;
	if (sc == NULL || sc->sc_chip == NULL || sc->sc_chip->rx_desc == NULL ||
	    sc->sc_chip->caps.rx_desc_size != 24 || data == NULL || length == 0 ||
	    length > RTW88_SDIO_RX_BATCH_MAX)
		return EINVAL;
	/* First pass: no candidate metadata is made visible. */
	for (offset = 0, count = 0; offset < length; offset += advance) {
		if (count == RTW88_SDIO_RX_RECORDS_MAX)
			return EOVERFLOW;
		info = (struct rtw88_rx_info){ 0 };
		error = rtw88_sdio_rx_record(sc, data + offset, length - offset,
		    &info, &advance);
		if (error != 0)
			return error;
		count++;
	}
	/* No 512-record temporary is allocated on the kernel stack. The pure
	 * decoder and immutable input produce the same metadata in this pass. */
	for (offset = 0, index = 0; index < count; offset += advance, index++) {
		info = (struct rtw88_rx_info){ 0 };
		error = rtw88_sdio_rx_record(sc, data + offset, length - offset,
		    &info, &advance);
		if (error != 0)
			return error;
		out->records[index].descriptor_offset = (uint16_t)offset;
		out->records[index].payload_offset = (uint16_t)(offset + info.payload_offset);
		out->records[index].length = info.packet_size;
		out->records[index].info = info;
	}
	if (offset != length)
		return EBADMSG;
	out->count = (uint16_t)count;
	return 0;
}
