/* Origin: ADI no-OS ce7107f957d19858f2dffc85bf3d7f2c70367000; native EmberBSD adaptation. */
/***************************************************************************//**
 *   @file   common.h
 *   @brief  Header file of Common Driver.
 *   @author DBogdan (dragos.bogdan@analog.com)
********************************************************************************
 * Copyright 2013(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/
#ifndef COMMON_H_
#define COMMON_H_

#include <sys/stdint.h>
#include "no_os_error.h"

#if defined (__STDC__) && (__STDC_VERSION__ >= 199901L) || defined(__cplusplus)
#include <sys/types.h>
#else
typedef enum { false, true } bool;
#endif

struct no_os_clk {
	const char	*name;
	uint32_t	rate;
};

struct no_os_clk_hw {
	struct no_os_clk *clk;
};

#endif
