/* Copyright 2013(c) Analog Devices, Inc. */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Origin: ADI no-OS ce7107f957d19858f2dffc85bf3d7f2c70367000; native EmberBSD adaptation. */
#include <sys/param.h>
#include <sys/systm.h>
#include "ad9361.h"
/**
 * HDL loopback enable/disable.
 * @param phy The AD9361 state structure.
 * @param enable Enable/disable option.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_hdl_loopback(struct ad9361_rf_phy *phy, bool enable)
{
	return -ENODEV;
}

/**
 * Digital tune.
 * @param phy The AD9361 state structure.
 * @param max_freq Maximum frequency.
 * @param flags Flags: BE_VERBOSE, BE_MOREVERBOSE, DO_IDELAY, DO_ODELAY.
 * @return 0 in case of success, negative error code otherwise.
 */
int32_t ad9361_dig_tune(struct ad9361_rf_phy *phy, uint32_t max_freq,
			enum dig_tune_flags flags)
{
	return -EOPNOTSUPP;
}

/**
* Setup the AD9361 device.
* @param phy The AD9361 state structure.
* @return 0 in case of success, negative error code otherwise.
*/
int32_t ad9361_post_setup(struct ad9361_rf_phy *phy)
{
	return -EOPNOTSUPP;
}
