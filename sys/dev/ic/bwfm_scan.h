/* SPDX-License-Identifier: BSD-2-Clause */
/* Firmware scan metadata. Include bwfmreg.h before this header. */
#ifndef _DEV_IC_BWFM_SCAN_H_
#define _DEV_IC_BWFM_SCAN_H_

/* Decode the primary 20 MHz channel, never the current host scan channel. */
static inline unsigned int
bwfm_scan_channel(uint8_t control, uint16_t spec, unsigned int io_type)
{
	int channel = spec & BWFM_CHANSPEC_CHAN_MASK;
	unsigned int sideband;

	if (control != 0)
		return control;
	if (io_type == 1) {
		switch (spec & BWFM_CHANSPEC_D11N_BW_MASK) {
		case BWFM_CHANSPEC_D11N_BW_20:
			if ((spec & BWFM_CHANSPEC_D11N_SB_MASK) !=
			    BWFM_CHANSPEC_D11N_SB_N)
				return 0;
			break;
		case BWFM_CHANSPEC_D11N_BW_40:
			switch (spec & BWFM_CHANSPEC_D11N_SB_MASK) {
			case BWFM_CHANSPEC_D11N_SB_L:
				channel -= 2;
				break;
			case BWFM_CHANSPEC_D11N_SB_U:
				channel += 2;
				break;
			default:
				return 0;
			}
			break;
		default:
			return 0;
		}
	} else if (io_type == 2) {
		sideband = (spec & BWFM_CHANSPEC_D11AC_SB_MASK) >>
		    BWFM_CHANSPEC_D11AC_SB_SHIFT;
		switch (spec & BWFM_CHANSPEC_D11AC_BW_MASK) {
		case BWFM_CHANSPEC_D11AC_BW_20:
			if (sideband != 0)
				return 0;
			break;
		case BWFM_CHANSPEC_D11AC_BW_40:
			if (sideband > 1)
				return 0;
			channel += (int)sideband * 4 - 2;
			break;
		case BWFM_CHANSPEC_D11AC_BW_80:
			if ((spec & BWFM_CHANSPEC_D11AC_BND_MASK) !=
			    BWFM_CHANSPEC_D11AC_BND_5G || sideband > 3)
				return 0;
			channel += (int)sideband * 4 - 6;
			break;
		case BWFM_CHANSPEC_D11AC_BW_160:
			if ((spec & BWFM_CHANSPEC_D11AC_BND_MASK) !=
			    BWFM_CHANSPEC_D11AC_BND_5G)
				return 0;
			channel += (int)sideband * 4 - 14;
			break;
		default:
			return 0;
		}
	} else {
		return 0;
	}
	if (channel <= 0 || channel > 255)
		return 0;
	if (io_type == 1) {
		if ((spec & BWFM_CHANSPEC_D11N_BND_MASK) !=
		    (channel <= 14 ? BWFM_CHANSPEC_D11N_BND_2G :
		    BWFM_CHANSPEC_D11N_BND_5G))
			return 0;
	} else {
		if ((spec & BWFM_CHANSPEC_D11AC_BND_MASK) !=
		    (channel <= 14 ? BWFM_CHANSPEC_D11AC_BND_2G :
		    BWFM_CHANSPEC_D11AC_BND_5G))
			return 0;
	}
	return (unsigned int)channel;
}

/* NetBSD's legacy scan ABI carries unsigned quality, not signed dBm. */
static inline unsigned int
bwfm_scan_rssi(int16_t dbm)
{
	int quality = dbm + 100;

	return quality < 1 ? 1 : quality > 100 ? 100 : (unsigned int)quality;
}
#endif
