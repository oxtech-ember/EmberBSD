/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/display/{icl_dsi.c,intel_dsi.c,
 * intel_dsi_dcs_backlight.c,intel_dsi_vbt.c,vlv_dsi.c,vlv_dsi_pll.c} (the members they
 * read and write, the calls and the arguments); dist618/drm/drm_mipi_dsi.c was not
 * imported; the MIPI DSI and DCS specifications for the data types and the packet
 * layout; the GPL original header was not read.
 *
 * The MIPI DSI bus as the i915 panel code of 6.18 uses it: the messages, the packet a message
 * becomes, the host operations a driver provides and the device of a panel.  The functions
 * that do not need a device (the size of a pixel format, the classes of a data type, the packet of
 * a message) are here with their behaviour; the calls that send to a panel through a host
 * (generic and DCS writes and reads) are declared, not defined: there is no DSI bus core yet.
 * The device tree parts of a DSI host do not exist here.
 */
#ifndef _DRM618_DRM_MIPI_DSI_H_
#define _DRM618_DRM_MIPI_DSI_H_

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>

#include <video/mipi_display.h>

struct device;
struct drm_dsc_config;
struct mipi_dsi_host;
struct mipi_dsi_device;

/* flags of a message */
#define	MIPI_DSI_MSG_REQ_ACK	(1 << 0)	/* the peripheral acknowledges */
#define	MIPI_DSI_MSG_USE_LPM	(1 << 1)	/* send in low power mode */

struct mipi_dsi_msg {
	u8		channel;
	u8		type;
	u16		flags;
	size_t		tx_len;
	const void	*tx_buf;
	size_t		rx_len;
	void		*rx_buf;
};

/* the bytes on the wire: header (virtual channel and data type, then 2 bytes), payload */
struct mipi_dsi_packet {
	size_t		size;
	u8		header[4];
	size_t		payload_length;
	const u8	*payload;
};

/* data types of the packets (the DSI specification) */
#define	MIPI_DSI_V_SYNC_START			0x01
#define	MIPI_DSI_V_SYNC_END			0x11
#define	MIPI_DSI_H_SYNC_START			0x21
#define	MIPI_DSI_H_SYNC_END			0x31
#define	MIPI_DSI_COMPRESSION_MODE		0x07
#define	MIPI_DSI_END_OF_TRANSMISSION		0x08
#define	MIPI_DSI_COLOR_MODE_OFF			0x02
#define	MIPI_DSI_COLOR_MODE_ON			0x12
#define	MIPI_DSI_SHUTDOWN_PERIPHERAL		0x22
#define	MIPI_DSI_TURN_ON_PERIPHERAL		0x32
#define	MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM	0x03
#define	MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM	0x13
#define	MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM	0x23
#define	MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM	0x04
#define	MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM	0x14
#define	MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM	0x24
#define	MIPI_DSI_DCS_SHORT_WRITE		0x05
#define	MIPI_DSI_DCS_SHORT_WRITE_PARAM		0x15
#define	MIPI_DSI_DCS_READ			0x06
#define	MIPI_DSI_EXECUTE_QUEUE			0x16
#define	MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE	0x37
#define	MIPI_DSI_NULL_PACKET			0x09
#define	MIPI_DSI_BLANKING_PACKET		0x19
#define	MIPI_DSI_GENERIC_LONG_WRITE		0x29
#define	MIPI_DSI_DCS_LONG_WRITE			0x39
#define	MIPI_DSI_PICTURE_PARAMETER_SET		0x0a
#define	MIPI_DSI_COMPRESSED_PIXEL_STREAM	0x0b
#define	MIPI_DSI_LOOSELY_PACKED_PIXEL_STREAM_YCBCR20	0x0c
#define	MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR24	0x1c
#define	MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR16	0x2c
#define	MIPI_DSI_PACKED_PIXEL_STREAM_30		0x0d
#define	MIPI_DSI_PACKED_PIXEL_STREAM_36		0x1d
#define	MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR12	0x3d
#define	MIPI_DSI_PACKED_PIXEL_STREAM_16		0x0e
#define	MIPI_DSI_PACKED_PIXEL_STREAM_18		0x1e
#define	MIPI_DSI_PIXEL_STREAM_3BYTE_18		0x2e
#define	MIPI_DSI_PACKED_PIXEL_STREAM_24		0x3e

/* the host: its operations, as the driver of a controller provides them */
struct mipi_dsi_host_ops {
	int	(*attach)(struct mipi_dsi_host *, struct mipi_dsi_device *);
	int	(*detach)(struct mipi_dsi_host *, struct mipi_dsi_device *);
	ssize_t	(*transfer)(struct mipi_dsi_host *, const struct mipi_dsi_msg *);
};

struct mipi_dsi_host {
	struct device				*dev;
	const struct mipi_dsi_host_ops		*ops;
	struct list_head			list;
};

/* the format of the pixels on the link */
enum mipi_dsi_pixel_format {
	MIPI_DSI_FMT_RGB888,
	MIPI_DSI_FMT_RGB666,
	MIPI_DSI_FMT_RGB666_PACKED,
	MIPI_DSI_FMT_RGB565,
};

/* modes of the link */
#define	MIPI_DSI_MODE_VIDEO			BIT(0)
#define	MIPI_DSI_MODE_VIDEO_BURST		BIT(1)
#define	MIPI_DSI_MODE_VIDEO_SYNC_PULSE		BIT(2)
#define	MIPI_DSI_MODE_VIDEO_AUTO_VERT		BIT(3)
#define	MIPI_DSI_MODE_VIDEO_HSE			BIT(4)
#define	MIPI_DSI_MODE_VIDEO_NO_HFP		BIT(5)
#define	MIPI_DSI_MODE_VIDEO_NO_HBP		BIT(6)
#define	MIPI_DSI_MODE_VIDEO_NO_HSA		BIT(7)
#define	MIPI_DSI_MODE_VSYNC_FLUSH		BIT(8)
#define	MIPI_DSI_MODE_NO_EOT_PACKET		BIT(9)
#define	MIPI_DSI_CLOCK_NON_CONTINUOUS		BIT(10)
#define	MIPI_DSI_MODE_LPM			BIT(11)
#define	MIPI_DSI_HS_PKT_END_ALIGNED		BIT(12)

/* the device of a panel on a host */
struct mipi_dsi_device {
	struct mipi_dsi_host		*host;
	char				name[20];
	unsigned int			channel;
	unsigned int			lanes;
	enum mipi_dsi_pixel_format	format;
	unsigned long			mode_flags;
	unsigned long			hs_rate;
	unsigned long			lp_rate;
	bool				attached;
	struct drm_dsc_config		*dsc;
};

/* a short packet carries its data in the header, a long one has a payload */
static inline bool
mipi_dsi_packet_format_is_short(u8 type)
{

	switch (type) {
	case MIPI_DSI_V_SYNC_START:
	case MIPI_DSI_V_SYNC_END:
	case MIPI_DSI_H_SYNC_START:
	case MIPI_DSI_H_SYNC_END:
	case MIPI_DSI_COMPRESSION_MODE:
	case MIPI_DSI_END_OF_TRANSMISSION:
	case MIPI_DSI_COLOR_MODE_OFF:
	case MIPI_DSI_COLOR_MODE_ON:
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
	case MIPI_DSI_TURN_ON_PERIPHERAL:
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_DCS_READ:
	case MIPI_DSI_EXECUTE_QUEUE:
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		return true;
	}
	return false;
}

static inline bool
mipi_dsi_packet_format_is_long(u8 type)
{

	switch (type) {
	case MIPI_DSI_NULL_PACKET:
	case MIPI_DSI_BLANKING_PACKET:
	case MIPI_DSI_GENERIC_LONG_WRITE:
	case MIPI_DSI_DCS_LONG_WRITE:
	case MIPI_DSI_PICTURE_PARAMETER_SET:
	case MIPI_DSI_COMPRESSED_PIXEL_STREAM:
	case MIPI_DSI_LOOSELY_PACKED_PIXEL_STREAM_YCBCR20:
	case MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR24:
	case MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR16:
	case MIPI_DSI_PACKED_PIXEL_STREAM_30:
	case MIPI_DSI_PACKED_PIXEL_STREAM_36:
	case MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR12:
	case MIPI_DSI_PACKED_PIXEL_STREAM_16:
	case MIPI_DSI_PACKED_PIXEL_STREAM_18:
	case MIPI_DSI_PIXEL_STREAM_3BYTE_18:
	case MIPI_DSI_PACKED_PIXEL_STREAM_24:
		return true;
	}
	return false;
}

/* bits per pixel of a format, -EINVAL for none */
static inline int
mipi_dsi_pixel_format_to_bpp(enum mipi_dsi_pixel_format fmt)
{

	switch (fmt) {
	case MIPI_DSI_FMT_RGB888:
		return 24;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
		return 18;
	case MIPI_DSI_FMT_RGB565:
		return 16;
	}
	return -EINVAL;
}

/*
 * The packet of a message: header byte 0 is the virtual channel (2 bits, above) and the data type
 * (6 bits); a long packet has the length of the payload in bytes 1 and 2 (low byte first), a
 * short one the first two bytes of the data, and no payload.  -EINVAL for a channel above 3, a
 * type that is neither short nor long, a short type with more than two bytes, or a long type
 * with a payload longer than 65535 bytes.
 */
static inline int
mipi_dsi_create_packet(struct mipi_dsi_packet *packet,
    const struct mipi_dsi_msg *msg)
{
	const u8 *tx = msg->tx_buf;

	if (msg->channel > 3)
		return -EINVAL;
	packet->header[0] = (u8)(((msg->channel & 0x3) << 6) | (msg->type & 0x3f));
	packet->header[1] = packet->header[2] = packet->header[3] = 0;
	if (mipi_dsi_packet_format_is_long(msg->type)) {
		if (msg->tx_len > 0xffff)
			return -EINVAL;
		packet->header[1] = msg->tx_len & 0xff;
		packet->header[2] = (msg->tx_len >> 8) & 0xff;
		packet->payload_length = msg->tx_len;
		packet->payload = tx;
	} else if (mipi_dsi_packet_format_is_short(msg->type)) {
		if (msg->tx_len > 2)
			return -EINVAL;
		if (msg->tx_len > 0)
			packet->header[1] = tx[0];
		if (msg->tx_len > 1)
			packet->header[2] = tx[1];
		packet->payload_length = 0;
		packet->payload = NULL;
	} else {
		return -EINVAL;
	}
	packet->size = 4 + packet->payload_length;
	return 0;
}

/* the bus, to a panel through a host (declared, not defined: there is no DSI core) */
int	mipi_dsi_host_register(struct mipi_dsi_host *);
void	mipi_dsi_host_unregister(struct mipi_dsi_host *);
int	mipi_dsi_attach(struct mipi_dsi_device *);
int	mipi_dsi_detach(struct mipi_dsi_device *);
int	mipi_dsi_set_maximum_return_packet_size(struct mipi_dsi_device *, u16);
ssize_t	mipi_dsi_generic_write(struct mipi_dsi_device *, const void *, size_t);
ssize_t	mipi_dsi_dcs_write_buffer(struct mipi_dsi_device *, const void *, size_t);
ssize_t	mipi_dsi_dcs_write(struct mipi_dsi_device *, u8, const void *, size_t);
ssize_t	mipi_dsi_dcs_read(struct mipi_dsi_device *, u8, void *, size_t);
int	mipi_dsi_dcs_nop(struct mipi_dsi_device *);
struct drm_dsc_picture_parameter_set;
int	mipi_dsi_picture_parameter_set(struct mipi_dsi_device *, const struct drm_dsc_picture_parameter_set *);
int	mipi_dsi_compression_mode(struct mipi_dsi_device *, bool);

#endif
