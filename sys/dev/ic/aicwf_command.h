/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AIC8800 FullMAC host commands and firmware replies. The D80 SDIO
 * transport adds its CRC after encoding. The DC USB transport uses zero
 * in that byte. See the Luckfox USB SDK, revision 6dcbca0d8a51,
 * aic8800_fdrv/rwnx_cmds.c, lmac_msg.h and ipc_shared.h.
 */
#ifndef _DEV_IC_AICWF_COMMAND_H_
#define _DEV_IC_AICWF_COMMAND_H_

#define	AICWF_TYPE_CFG			0x10	/* configuration frame bit */
#define	AICWF_TYPE_DATA			0x01
#define	AICWF_TXDESC_LEN		28
#define	AICWF_TYPE_TX_CFM		0x12
#define	AICWF_RXHDR_LEN			60	/* radio descriptor and pad */
#define	AICWF_TYPE_MSG			0x11
#define	AICWF_HDR_LEN			4
#define	AICWF_TX_MSG_OFFSET		8
#define	AICWF_MSG_HDR_LEN		8
#define	AICWF_RX_MSG_PARAM		12	/* includes the pattern word */
#define	AICWF_CO_OP_IN_PROGRESS		9	/* FullMAC operation is pending */
#define	AICWF_MM_GET_MAC_ADDR_REQ	0x0073
#define	AICWF_MM_ADD_IF_REQ		0x0006
#define	AICWF_MM_REMOVE_IF_REQ		0x0008
#define	AICWF_SCANU_START_REQ		0x1000
#define	AICWF_MM_GET_MAC_ADDR_CFM	0x0074
#define	AICWF_MM_ADD_IF_CFM		0x0007
#define	AICWF_MM_REMOVE_IF_CFM		0x0009
#define	AICWF_SCANU_START_CFM		0x1001	/* the scan is over */
#define	AICWF_SCANU_START_ACK		0x1009	/* the scan has begun */
#define	AICWF_SCANU_RESULT_IND		0x1004
#define	AICWF_TASK_DRIVER		100
#define	AICWF_MM_VERSION_REQ		0x0004
#define	AICWF_MM_VERSION_CFM		0x0005
#define	AICWF_MM_SET_STACK_START_REQ	0x007b
#define	AICWF_MM_SET_STACK_START_CFM	0x007c
#define	AICWF_VERSION_LEN		28
#define	AICWF_DBG_MEM_READ_REQ		0x0400
#define	AICWF_DBG_MEM_READ_CFM		0x0401
#define	AICWF_DBG_ERROR_IND		0x0408

struct aicwf_rx_frame {
	size_t len;	/* payload length after the configuration or data header */
	uint8_t type;
};

/*
 * Return the size to consume, zero at transfer padding, or -1 for a
 * truncated frame. The last frame can end before the alignment padding.
 */
static inline int
aicwf_frame_decode(const uint8_t *buf, size_t len, struct aicwf_rx_frame *frame)
{
	size_t payload, hdr, step;

	if (len < AICWF_HDR_LEN)
		return 0;
	payload = le16dec(buf);
	if (payload == 0)
		return 0;
	frame->type = buf[2] & 0x7f;
	hdr = (frame->type & AICWF_TYPE_CFG) ? AICWF_HDR_LEN : AICWF_RXHDR_LEN;
	if (hdr > len || payload > len - hdr)
		return -1;
	frame->len = payload;
	step = (hdr + payload + 3) & ~(size_t)3;
	return step < len ? (int)step : (int)len;
}

struct aicwf_version {
	uint32_t lmac;
	uint32_t machw[2];
	uint32_t phy[2];
	uint32_t features;
	uint16_t max_sta;
	uint8_t max_vif;
};

static inline void
aicwf_version_decode(const uint8_t cfm[AICWF_VERSION_LEN],
    struct aicwf_version *fw_version)
{
	fw_version->lmac = le32dec(cfm);
	fw_version->machw[0] = le32dec(cfm + 4);
	fw_version->machw[1] = le32dec(cfm + 8);
	fw_version->phy[0] = le32dec(cfm + 12);
	fw_version->phy[1] = le32dec(cfm + 16);
	fw_version->features = le32dec(cfm + 20);
	fw_version->max_sta = le16dec(cfm + 24);
	fw_version->max_vif = cfm[26];
}

static inline size_t
aicwf_command_encode(uint8_t *buf, size_t capacity, uint16_t id,
    const void *param, size_t param_len)
{
	const size_t hdr = AICWF_TX_MSG_OFFSET + AICWF_MSG_HDR_LEN;

	if (capacity < hdr || param_len > capacity - hdr ||
	    param_len > 0xfff - (hdr - AICWF_HDR_LEN))
		return 0;
	memset(buf, 0, hdr);
	le16enc(buf, hdr - AICWF_HDR_LEN + param_len);
	buf[2] = AICWF_TYPE_MSG;
	le16enc(buf + AICWF_TX_MSG_OFFSET, id);
	le16enc(buf + AICWF_TX_MSG_OFFSET + 2, id >> 10);
	le16enc(buf + AICWF_TX_MSG_OFFSET + 4, AICWF_TASK_DRIVER);
	le16enc(buf + AICWF_TX_MSG_OFFSET + 6, param_len);
	if (param_len != 0)
		memcpy(buf + hdr, param, param_len);
	return hdr + param_len;
}

static inline bool
aicwf_reply_decode(const uint8_t *msg, size_t len, uint16_t *id,
    uint16_t *param_len)
{
	if (len < AICWF_RX_MSG_PARAM)
		return false;
	*id = le16dec(msg);
	*param_len = le16dec(msg + 6);
	return *param_len <= len - AICWF_RX_MSG_PARAM;
}

static inline bool
aicwf_read_confirm(const uint8_t *param, size_t len, uint32_t addr,
    uint32_t *value)
{
	if (len != 8 || le32dec(param) != addr)
		return false;
	*value = le32dec(param + 4);
	return true;
}
#endif /* _DEV_IC_AICWF_COMMAND_H_ */
