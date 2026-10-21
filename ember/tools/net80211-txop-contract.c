/* SPDX-License-Identifier: BSD-2-Clause */
/* Execute native wire consumers. Only the kernel environment is modeled. */
#include <sys/types.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define __packed __attribute__((__packed__))
#define WME_NUM_AC 4
#define WME_AC_BE 0
#define IEEE80211_ELEMID_VENDOR 221
#define IFNAMSIZ 16
#define IEEE80211_C_WME 1
#define IEEE80211_DISCARD_IE(ic, flags, wh, ...) ((void)(wh))
#define LE_READ_2(p) ((uint16_t)((p)[0] | (uint16_t)(p)[1] << 8))
struct ieee80211com;
#include "native-txop.inc"

int
main(void)
{
	static const uint16_t values[] = {0, 255, 256, 2047, 2048,
	    32767, 32768, 65535};
	struct ieee80211com ic;
	struct ieee80211_node ni;
	uint8_t frame[sizeof(struct ieee80211_wme_param)];
	unsigned checks = 0, failures = 0;
	size_t i;
	int ac;

	for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		memset(&ic, 0, sizeof(ic));
		memset(&ni, 0, sizeof(ni));
		ic.ic_bss = &ni;
		for (ac = 0; ac < WME_NUM_AC; ac++) {
			struct wmeParams p = {0, 2, 3, 4, values[i], 0};
			ic.ic_wme.wme_bssChanParams.cap_wmeParams[ac] = p;
		}
		ic.ic_wme.wme_bssChanParams.cap_info = 1;
		checks++;
		failures += ieee80211_add_wme_param(frame, &ic.ic_wme) != frame + sizeof(frame);
		checks++;
		failures += ieee80211_parse_wmeparams(&ic, frame, NULL) != 1;
		for (ac = 0; ac < WME_NUM_AC; ac++) {
			const size_t off = offsetof(struct ieee80211_wme_param, params_acParams) + ac * 4 + 2;
			checks += 2;
			failures += LE_READ_2(frame + off) != values[i];
			failures += ic.ic_wme.wme_wmeChanParams.cap_wmeParams[ac].wmep_txopLimit != values[i];
		}
		ic.ic_caps = IEEE80211_C_WME;
		for (ac = 0; ac < WME_NUM_AC; ac++) {
			struct ieee80211req req = { .i_type = IEEE80211_IOC_WME_TXOPLIMIT,
			    .i_val = (int16_t)values[i], .i_len = (uint16_t)ac };
			unsigned bss;
			for (bss = 0; bss < 2; bss++) {
				req.i_val = (int16_t)values[i];
				req.i_len = (uint16_t)(ac | (bss ? IEEE80211_WMEPARAM_BSS : 0));
				checks += 4;
				failures += ieee80211_ioctl_setwmeparam(&ic, &req) != 0;
				failures += (bss ? ic.ic_wme.wme_bssChanParams.cap_wmeParams[ac].wmep_txopLimit :
				    ic.ic_wme.wme_chanParams.cap_wmeParams[ac].wmep_txopLimit) != values[i];
				req.i_val = 0;
				failures += ieee80211_ioctl_getwmeparam(&ic, &req) != 0;
				failures += (uint16_t)req.i_val != values[i];
			}
		}
	}
	printf("TXOP wire: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
