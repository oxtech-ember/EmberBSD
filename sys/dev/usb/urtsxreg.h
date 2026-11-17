/* Origin: EmberBSD Realtek USB register definitions (AI-assisted). */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_USB_URTSXREG_H_
#define _DEV_USB_URTSXREG_H_

/* USB controller ABI. Several CARD register addresses differ from PCI rtsx. */
#define URTSX_PPBUF		0xfa00
#define URTSX_FPDCTL		0xfc00
#define URTSX_HW_VERSION		0xfc01
#define URTSX_CLK_DIV		0xfc03
#define URTSX_SFSM		0xfc04
#define URTSX_SSC_DIV_N		0xfc07
#define URTSX_SSC_CTL1		0xfc09
#define URTSX_SSC_CTL2		0xfc0a
#define URTSX_CFG_MODE0		0xfc0e
#define URTSX_CFG_MODE1		0xfc0f
#define URTSX_CD_DEGLITCH_WIDTH	0xfc20
#define URTSX_CD_DEGLITCH_CTL	0xfc21
#define URTSX_CARD_CLK_SOURCE	0xfc2e
#define URTSX_DUMMY0		0xfc30
#define URTSX_CARD_SHARE		0xfd51
#define URTSX_CARD_DRIVE		0xfd52
#define URTSX_CARD_STOP		0xfd53
#define URTSX_CARD_OUTPUT		0xfd54
#define URTSX_SD30_DRIVE		0xfd57
#define URTSX_CARD_DMA1_CTL	0xfd5c
#define URTSX_CARD_DATA_SOURCE	0xfd5d
#define URTSX_CARD_SELECT		0xfd5e
#define URTSX_CARD_PULL_CTL1	0xfd60
#define URTSX_CARD_PULL_CTL5	0xfd64
#define URTSX_CARD_PULL_CTL6	0xfd65
#define URTSX_CARD_EXIST		0xfd6f
#define URTSX_CARD_INT_PENDING	0xfd71
#define URTSX_CARD_CLOCK		0xfd79
#define URTSX_CARD_POWER		0xfd7a
#define URTSX_LDO_POWER_CFG	0xfd7b
#define URTSX_OCP_STATUS		0xfd83
#define URTSX_SD_CFG1		0xfda0
#define URTSX_SD_CFG2		0xfda1
#define URTSX_SD_STATUS1		0xfda3
#define URTSX_SD_STATUS2		0xfda4
#define URTSX_SD_PAD_CTL		0xfda6
#define URTSX_SD_SAMPLE_CTL	0xfda7
#define URTSX_SD_PUSH_CTL		0xfda8
#define URTSX_SD_CMD0		0xfda9
#define URTSX_SD_BYTE_COUNT_L	0xfdaf
#define URTSX_SD_BYTE_COUNT_H	0xfdb0
#define URTSX_SD_BLOCK_COUNT_L	0xfdb1
#define URTSX_SD_BLOCK_COUNT_H	0xfdb2
#define URTSX_SD_TRANSFER		0xfdb3
#define URTSX_HS_PHY_CTL		0xfe26
#define URTSX_HS_PHY_DATA		0xfe27
#define URTSX_HS_PHY_LOAD		0xfe28
#define URTSX_FIFO_CTL		0xff02
#define URTSX_DMA_RESET		0xff15

#endif /* _DEV_USB_URTSXREG_H_ */
