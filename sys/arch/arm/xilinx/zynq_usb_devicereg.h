/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _ARM_XILINX_ZYNQ_USB_DEVICEREG_H_
#define _ARM_XILINX_ZYNQ_USB_DEVICEREG_H_
#define ZUDC_CMD 0x140
#define ZUDC_STS 0x144
#define ZUDC_INTR 0x148
#define ZUDC_ADDR 0x154
#define ZUDC_LIST 0x158
#define ZUDC_ULPI 0x170
#define ZUDC_PORT 0x184
#define ZUDC_OTG 0x1a4
#define ZUDC_MODE 0x1a8
#define ZUDC_SETUP 0x1ac
#define ZUDC_PRIME 0x1b0
#define ZUDC_FLUSH 0x1b4
#define ZUDC_STAT 0x1b8
#define ZUDC_COMPLETE 0x1bc
#define ZUDC_EPCTRL(n) (0x1c0 + 4 * (n))
#define ZUDC_RUN 0x00000001U
#define ZUDC_RESET 0x00000002U
#define ZUDC_SUTW 0x00002000U
#define ZUDC_ATDTW 0x00004000U
#define ZUDC_INT_UI 0x00000001U
#define ZUDC_INT_UEI 0x00000002U
#define ZUDC_INT_PCI 0x00000004U
#define ZUDC_INT_URI 0x00000040U
#define ZUDC_INT_SLI 0x00000100U
#define ZUDC_INT_MASK 0x00000147U
#define ZUDC_PORT_CCS 0x00000001U
#define ZUDC_PORT_HS 0x08000000U
#define ZUDC_TD_ACTIVE 0x00000080U
#define ZUDC_TD_ERRORS 0x00000068U
#define ZUDC_TD_IOC 0x00008000U
#define ZUDC_TD_TERM 1U
#define ZUDC_QH_IOS 0x00008000U
#define ZUDC_QH_ZLT 0x20000000U
#define ZUDC_QH_ALIGN 2048
#define ZUDC_TD_ALIGN 32
#define ZUDC_EP_COUNT 16
#define ZUDC_SLOTS (ZUDC_EP_COUNT * 2)
#define ZUDC_POLL_US 10000
struct zudc_td {
	uint32_t next, token, page[5], reserved;
};
struct zudc_qh {
	uint32_t cap, current, next, token, page[5], reserved;
	uint8_t setup[8];
	uint32_t pad[4];
};
#endif
