/* Origin: EmberBSD UPort 1150 interface-mode ioctls, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_USB_UMOXA1150IO_H_
#define _DEV_USB_UMOXA1150IO_H_
#include <sys/ioccom.h>
#define UMOXA1150_MODE_RS232 0
#define UMOXA1150_MODE_RS485_2W 1
#define UMOXA1150_MODE_RS422_4W 2
#define UMOXA1150_SET_MODE _IOW('M', 115, int)
#define UMOXA1150_GET_FAULT _IOR('M', 116, int)
#endif
