/* SPDX-License-Identifier: BSD-2-Clause */
/* Native driver functions; fake sinks record their bus and firmware effects. */
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole16(x) OSSwapHostToLittleInt16(x)
#else
#include <endian.h>
#endif
#define __packed __attribute__((__packed__))
#define WME_NUM_AC 4
enum { WME_AC_BE, WME_AC_BK, WME_AC_VI, WME_AC_VO };
#define IEEE80211_TXOP_TO_US(x) ((x) << 5)
#define IEEE80211_F_WME 1
#define IEEE80211_F_SHSLOT 2
struct ieee80211com;
#include "native-txop-types.inc"
struct ifnet { void *if_softc; };
struct ieee80211_node {
	unsigned ni_flags, ni_associd, ni_dtim_period, ni_intval;
	uint8_t ni_bssid[6];
};
struct ieee80211com {
	struct ieee80211_wme_state ic_wme;
	struct ifnet *ic_ifp;
	unsigned ic_flags;
	unsigned ic_opmode;
	uint8_t ic_myaddr[6];
	struct ieee80211_node *ic_bss;
};
struct ath_txq { unsigned axq_qnum; };
struct ath_hal { unsigned unused; };
typedef struct {
	uint32_t tqi_aifs, tqi_cwmin, tqi_cwmax, tqi_burstTime;
} HAL_TXQ_INFO;
struct model_softc {
	struct ieee80211com sc_ic;
	struct ath_txq *sc_ac2q[4];
	struct ath_hal *sc_ah;
	int sc_dev, sc_write_mtx;
	unsigned sc_flags, sc_use_mld_api, sc_device_family;
	struct { unsigned htop1, ampdu_param, vhtcaps; } sc_bss_ht;
};
#define ath_softc model_softc
#define rt2860_softc model_softc
#define rt2661_softc model_softc
#define run_softc model_softc
#define rtwn_softc model_softc
#define urtwn_softc model_softc
#define iwi_softc model_softc
#define iwn_softc model_softc
#define wpi_softc model_softc
static unsigned actions, scheduled;
static uint32_t registers[65536 / 4], burst[4];
static unsigned char firmware[256];
static _Alignas(struct wmeParams) unsigned char pending[256];
static size_t firmware_len;
static void (*pending_cb)(struct model_softc *, void *);
static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
static void
write_register(struct model_softc *sc, unsigned reg, uint32_t value)
{
	(void)sc;
	assert(reg / 4 < sizeof(registers) / sizeof(registers[0]));
	registers[reg / 4] = value;
	actions++;
}
static int
send_command(struct model_softc *sc, int command, void *data, size_t len, int async)
{
	(void)sc; (void)command; (void)async;
	assert(len <= sizeof(firmware));
	memcpy(firmware, data, len);
	firmware_len = len;
	actions++;
	return 0;
}
static void
run_do_async(struct model_softc *sc, void (*cb)(struct model_softc *, void *),
    void *data, int len)
{
	(void)sc;
	assert(len >= 0 && (size_t)len <= sizeof(pending));
	if (len != 0)
		memcpy(pending, data, (size_t)len);
	pending_cb = cb;
	scheduled++;
}
#define RAL_WRITE(sc, reg, value) write_register(sc, reg, value)
#define run_write write_register
#define rtwn_write_4 write_register
#define urtwn_write_4 write_register
#define iwi_cmd send_command
#define iwn_cmd send_command
#define wpi_cmd send_command
#define IWI_CMD_SET_WME_PARAMS 1
#define IWN_CMD_EDCA_PARAMS 2
#define WPI_CMD_SET_WME 3
#define DPRINTF(...) ((void)0)
#define DPRINTFN(...) ((void)0)
#define URTWNHIST_FUNC() ((void)0)
#define URTWNHIST_CALLED() ((void)0)
#define IC2IFP(ic) ((ic)->ic_ifp)
#define device_printf(...) ((void)0)
#define splnet() 0
#define splx(s) ((void)(s))
#define mutex_enter(m) ((void)(m))
#define mutex_exit(m) ((void)(m))
#define SM(field, value) (((value) << field##_S) & field##_M)
static void
ath_hal_gettxqueueprops(struct ath_hal *ah, unsigned q, HAL_TXQ_INFO *qi)
{
	(void)ah; (void)q;
	memset(qi, 0, sizeof(*qi));
	actions++;
}
static int
ath_hal_settxqueueprops(struct ath_hal *ah, unsigned q, HAL_TXQ_INFO *qi)
{
	(void)ah;
	assert(q < 4);
	burst[q] = qi->tqi_burstTime;
	actions++;
	return 1;
}
#define ath_hal_resettxqueue(ah, q) ((void)(ah), (void)(q), actions++)
#include "native-txop-drivers.inc"

static void
initialize(struct model_softc *sc, struct ifnet *ifp, struct ath_txq queues[4], uint16_t value)
{
	int ac;
	memset(sc, 0, sizeof(*sc));
	ifp->if_softc = sc;
	sc->sc_ic.ic_ifp = ifp;
	sc->sc_ic.ic_flags = IEEE80211_F_WME;
	for (ac = 0; ac < 4; ac++) {
		struct wmeParams param = {0, 2, 3, 4, value, 0};
		sc->sc_ic.ic_wme.wme_chanParams.cap_wmeParams[ac] = param;
		sc->sc_ic.ic_wme.wme_params[ac] = param;
		queues[ac].axq_qnum = (unsigned)ac;
		sc->sc_ac2q[ac] = &queues[ac];
	}
	actions = scheduled = 0;
	memset(registers, 0, sizeof(registers));
	memset(firmware, 0, sizeof(firmware));
	firmware_len = 0;
}

int
main(void)
{
	struct model_softc sc;
	struct ifnet ifp;
	struct ath_txq queues[4];
	static const uint16_t values[] = {0, 255, 256, 2047, 2048, 32767, 32768, 65535};
	int (*const intel[])(struct ieee80211com *) = { iwi_wme_update, iwn_wme_update, wpi_wme_update };
	size_t i, driver;
	int ac, result;

	for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		initialize(&sc, &ifp, queues, values[i]);
		result = ath_wme_update(&sc.sc_ic);
		CHECK(result == (values[i] > 32767 ? EOPNOTSUPP : 0));
		CHECK(values[i] > 32767 ? actions == 0 : burst[3] == (uint32_t)values[i] * 32);
		initialize(&sc, &ifp, queues, values[i]);
		result = rt2860_updateedca(&sc.sc_ic);
		CHECK(result == (values[i] > 255 ? EOPNOTSUPP : 0));
		CHECK(values[i] > 255 ? actions == 0 : (registers[RT2860_EDCA_AC_CFG(3) / 4] & 255) == values[i]);
		initialize(&sc, &ifp, queues, values[i]);
		result = run_updateedca(&sc.sc_ic);
		CHECK(result == (values[i] > 255 ? EOPNOTSUPP : 0));
		CHECK(values[i] > 255 ? scheduled == 0 : scheduled == 1);
		if (values[i] <= 255) {
			/* A later beacon/ioctl must not change the queued hardware set. */
			for (ac = 0; ac < 4; ac++)
				sc.sc_ic.ic_wme.wme_params[ac].wmep_txopLimit = values[7];
			assert(pending_cb != NULL);
			pending_cb(&sc, pending);
			CHECK((registers[RT2860_EDCA_AC_CFG(3) / 4] & 255) == values[i]);
		}
		for (driver = 0; driver < sizeof(intel) / sizeof(intel[0]); driver++) {
			initialize(&sc, &ifp, queues, values[i]);
			result = intel[driver](&sc.sc_ic);
			CHECK(result == (values[i] > 2047 ? EOPNOTSUPP : 0));
			CHECK(values[i] > 2047 ? actions == 0 : actions == 1);
			if (values[i] <= 2047) {
				uint16_t actual;
				size_t off = driver == 0 ? offsetof(struct iwi_wme_params, burst[3]) :
				    driver == 1 ? offsetof(struct iwn_edca_params, ac[3].txoplimit) :
				    offsetof(struct wpi_wme_setup, ac[3].txop);
				CHECK(firmware_len > off + 1);
				memcpy(&actual, firmware + off, sizeof(actual));
				CHECK(htole16(actual) == (uint32_t)values[i] * 32);
			}
		}
		initialize(&sc, &ifp, queues, values[i]);
		CHECK(rtwn_wme_update(&sc.sc_ic) == 0);
		CHECK(registers[R92C_EDCA_VO_PARAM / 4] >> 16 == values[i]);
		initialize(&sc, &ifp, queues, values[i]);
		urtwn_wme_update_cb(&sc, NULL);
		CHECK(registers[R92C_EDCA_VO_PARAM / 4] >> 16 == values[i]);
		initialize(&sc, &ifp, queues, values[i]);
		CHECK(rt2661_wme_update(&sc.sc_ic) == 0);
		CHECK(registers[RT2661_AC_TXOP_CSR1 / 4] == ((uint32_t)values[i] << 16 | values[i]));
	}
	/* An unsupported final AC rejects the whole set, including earlier ACs. */
	for (ac = 0; ac < 4; ac++) {
		initialize(&sc, &ifp, queues, 0);
		sc.sc_ic.ic_wme.wme_params[ac].wmep_txopLimit = values[2];
		CHECK(rt2860_updateedca(&sc.sc_ic) == EOPNOTSUPP);
		CHECK(actions == 0);
		CHECK(run_updateedca(&sc.sc_ic) == EOPNOTSUPP);
		CHECK(scheduled == 0);
		initialize(&sc, &ifp, queues, 0);
		sc.sc_ic.ic_wme.wme_chanParams.cap_wmeParams[ac].wmep_txopLimit = values[6];
		CHECK(ath_wme_update(&sc.sc_ic) == EOPNOTSUPP);
		CHECK(actions == 0);
		for (driver = 0; driver < sizeof(intel) / sizeof(intel[0]); driver++) {
			initialize(&sc, &ifp, queues, 0);
			sc.sc_ic.ic_wme.wme_chanParams.cap_wmeParams[ac].wmep_txopLimit = values[4];
			CHECK(intel[driver](&sc.sc_ic) == EOPNOTSUPP);
			CHECK(actions == 0);
		}
	}
	for (driver = 0; driver < sizeof(intel) / sizeof(intel[0]); driver++) {
		initialize(&sc, &ifp, queues, 65535);
		sc.sc_ic.ic_flags = 0;
		CHECK(intel[driver](&sc.sc_ic) == 0);
		CHECK(actions == 0);
	}
	printf("TXOP drivers: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
