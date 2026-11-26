/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AIC8800 FullMAC host commands and firmware replies. The D80 SDIO
 * transport adds its CRC after encoding. The DC USB transport uses zero
 * in that byte. See the Luckfox USB SDK, revision 6dcbca0d8a51,
 * aic8800_fdrv/rwnx_cmds.c, lmac_msg.h and ipc_shared.h.
 */
#ifndef _DEV_IC_AICWF_COMMAND_H_
#define _DEV_IC_AICWF_COMMAND_H_

#define	AICWF_TYPE_MSG			0x11
#define	AICWF_HDR_LEN			4
#define	AICWF_TX_MSG_OFFSET		8
#define	AICWF_MSG_HDR_LEN		8
#define	AICWF_RX_MSG_PARAM		12	/* includes the pattern word */
#define	AICWF_TASK_DRIVER		100
#define	AICWF_DBG_MEM_READ_REQ		0x0400
#define	AICWF_DBG_MEM_READ_CFM		0x0401

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
