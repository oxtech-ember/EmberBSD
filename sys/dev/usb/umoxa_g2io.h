/* Origin: EmberBSD experimental G2 per-port controls, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_USB_UMOXA_G2IO_H_
#define _DEV_USB_UMOXA_G2IO_H_
#include <sys/ioccom.h>
#define UMOXA_G2_SET_MODE _IOW('o', 117, int)
#define UMOXA_G2_GET_FAULT _IOR('o', 118, int)
#define UMOXA_G2_RS232 0
#define UMOXA_G2_RS485_2W 1
#define UMOXA_G2_RS422 2
#define UMOXA_G2_RS485_4W 3
#endif
