/* SPDX-License-Identifier: BSD-2-Clause */
/* Complete command producers; unrelated firmware and station state is modeled. */
#define main txop_simple_main
#include "net80211-txop-drivers-contract.c"
#undef main
#include <stdlib.h>
#if defined(__APPLE__)
#define htole32(x) OSSwapHostToLittleInt32(x)
#endif
#include "native-txop-intel-types.inc"
#ifndef IEEE80211_NO_HT
#define IEEE80211_NO_HT
#endif
#define IEEE80211_NODE_QOS 1
#define IEEE80211_F_SHPREAMBLE 4
#define IEEE80211_F_USEPROT 8
#define IEEE80211_M_STA 0
#define IEEE80211_M_MONITOR 1
#define IEEE80211_HTOP1_PROT_MASK 3
#define IEEE80211_ADDR_COPY(dst, src) memcpy(dst, src, 6)
#define IEEE80211_AID(id) ((id) & 0x3fff)
struct model_phy { unsigned id; };
struct model_node {
	struct ieee80211_node in_ni;
	unsigned in_id, in_color, in_ht;
	uint8_t in_macaddr[6];
	struct model_phy *in_phyctxt;
	struct { unsigned width; } in_htc;
	struct { unsigned mode; uint8_t rates[2]; } in_htt;
};
#define iwm_node model_node
#define iwx_node model_node
#define iwm_softc model_softc
#define iwx_softc model_softc
/* AC payload widths come from the real firmware headers. Other fields are stubs. */
struct model_mac {
	uint32_t id_and_color, action, mac_type, tsf_id, cck_rates, ofdm_rates;
	uint32_t cck_short_preamble, short_slot, qos_flags, protection_flags, filter_flags;
	uint8_t node_addr[6], bssid_addr[6];
	struct iwm_ac_qos ac[5];
	unsigned sta;
};
#define iwm_mac_ctx_cmd model_mac
#define iwx_mac_ctx_cmd model_mac
struct iwx_link_config_cmd {
	uint32_t link_id, mac_id, phy_id, active, cck_rates, ofdm_rates;
	uint32_t cck_short_preamble, short_slot, qos_flags, protection_flags;
	uint32_t bi, dtim_interval, modify_mask, flags, flags_mask, action;
	uint8_t local_link_addr[6], spec_link_id, listen_lmac;
	struct iwx_ac_qos ac[5];
};
struct iwx_sta_cfg_cmd_v2 {
	uint32_t sta_id, station_type, link_id, assoc_id;
	uint32_t mimo, tx_ampdu_spacing, tx_ampdu_max_size;
	uint8_t peer_mld_address[6], peer_link_address[6];
};
struct iwx_mvm_sta_cfg_cmd { unsigned dummy; };
struct iwx_mvm_remove_sta_cmd { uint32_t sta_id; };
enum { IWM_FW_CTXT_ACTION_STUB, IWM_FW_CTXT_ACTION_ADD,
    IWM_FW_CTXT_ACTION_MODIFY, IWM_FW_CTXT_ACTION_REMOVE };
#define IWX_FW_CTXT_ACTION_ADD IWM_FW_CTXT_ACTION_ADD
#define IWX_FW_CTXT_ACTION_MODIFY IWM_FW_CTXT_ACTION_MODIFY
#define IWX_FW_CTXT_ACTION_REMOVE IWM_FW_CTXT_ACTION_REMOVE
#define IWX_LINK_CONTEXT_MODIFY_ACTIVE (1 << 0)
#define IWX_LINK_CONTEXT_MODIFY_RATES_INFO (1 << 1)
#define IWX_LINK_CONTEXT_MODIFY_PROTECT_FLAGS (1 << 2)
#define IWX_LINK_CONTEXT_MODIFY_QOS_PARAMS (1 << 3)
#define IWX_LINK_CONTEXT_MODIFY_BEACON_TIMING (1 << 4)
#define IWM_FW_CMD_ID_AND_COLOR(id, color) ((id) | (color) << 8)
#define IWX_FW_CMD_ID_AND_COLOR IWM_FW_CMD_ID_AND_COLOR
#define IWM_FW_MAC_TYPE_BSS_STA 1
#define IWX_FW_MAC_TYPE_BSS_STA 1
#define IWX_FW_MAC_TYPE_LISTENER 2
#define IWM_TSF_ID_A 0
#define IWX_TSF_ID_A 0
#define IWM_MAC_FLG_SHORT_PREAMBLE (1 << 5)
#define IWM_MAC_FLG_SHORT_SLOT (1 << 4)
#define IWX_MAC_FLG_SHORT_PREAMBLE IWM_MAC_FLG_SHORT_PREAMBLE
#define IWX_MAC_FLG_SHORT_SLOT IWM_MAC_FLG_SHORT_SLOT
#define IWM_MAC_QOS_FLG_UPDATE_EDCA 1
#define IWX_MAC_QOS_FLG_UPDATE_EDCA 1
#define IWX_MAC_QOS_FLG_TGN 2
#define IWM_MAC_PROT_FLG_TGG_PROTECT 1
#define IWX_MAC_PROT_FLG_TGG_PROTECT 1
#define IWX_LINK_PROT_FLG_TGG_PROTECT 1
#define IWX_LINK_PROT_FLG_HT_PROT 2
#define IWX_LINK_PROT_FLG_FAT_PROT 4
#define IWM_MAC_FILTER_ACCEPT_GRP 1
#define IWM_MAC_FILTER_IN_BEACON 2
#define IWX_MAC_FILTER_ACCEPT_GRP 1
#define IWX_MAC_FILTER_IN_BEACON 2
#define IWX_MAC_FILTER_IN_PROMISC 4
#define IWX_MAC_FILTER_IN_CONTROL_AND_MGMT 8
#define IWX_MAC_FILTER_IN_PROBE_REQUEST 16
#define IWX_MAC_FILTER_IN_CRC32 32
#define IWX_FLAG_MAC_ACTIVE 8
#define IWX_DEVICE_FAMILY_BZ 3
#define IWX_HT_WIDTH_40 1
#define IWX_HT_TLC_MODE_VHT 1
#define IWX_FW_CTXT_INVALID 0xff
#define IWX_FW_CMD_VER_UNKNOWN 0xff
#define IWX_STATION_ID 0
#define IWX_MONITOR_STA_ID 1
#define IWX_STA_GENERAL_PURPOSE 0
#define IWX_STA_LINK 1
#define IWM_MAC_CONTEXT_CMD 0x28
#define IWX_MAC_CONTEXT_CMD 0x28
#define IWX_MAC_CONF_GROUP 3
#define IWX_LINK_CONFIG_CMD 9
#define IWX_STA_CONFIG_CMD 10
#define IWX_STA_REMOVE_CMD 12
#define IWX_WIDE_ID(group, cmd) ((group) << 8 | (cmd))
#define KASSERT assert
#define panic(...) abort()
#define device_xname(dev) "fixture"
static const uint8_t etherbroadcastaddr[6] = {255,255,255,255,255,255};
static const unsigned iwm_ac_to_tx_fifo[4] = {1,0,2,3};
static const unsigned iwx_ac_to_tx_fifo[4] = {1,0,2,3};
static const unsigned iwx_ac_to_bz_tx_fifo[4] = {1,0,2,3};
static struct model_mac saved_mac;
static struct iwx_link_config_cmd saved_link;
static int
intel_send(struct model_softc *sc, unsigned command, int flags, size_t len, const void *data)
{
	(void)sc; (void)flags;
	if (command == IWM_MAC_CONTEXT_CMD) {
		assert(len == sizeof(saved_mac));
		memcpy(&saved_mac, data, len);
	}
	if (command == IWX_WIDE_ID(IWX_MAC_CONF_GROUP, IWX_LINK_CONFIG_CMD)) {
		assert(len == sizeof(saved_link));
		memcpy(&saved_link, data, len);
	}
	actions++;
	return 0;
}
#define iwm_send_cmd_pdu intel_send
#define iwx_send_cmd_pdu intel_send
static void
ack_rates(struct model_softc *sc, struct model_node *in, int *cck, int *ofdm)
{
	(void)sc; (void)in;
	*cck = *ofdm = 0;
}
#define iwm_ack_rates ack_rates
#define iwx_ack_rates ack_rates
static void
fill_sta(struct model_softc *sc, struct model_node *in, unsigned *sta, int assoc)
{
	(void)sc; (void)in;
	*sta = (unsigned)assoc;
}
#define iwm_mac_ctxt_cmd_fill_sta fill_sta
#define iwx_mac_ctxt_cmd_fill_sta fill_sta
static int
iwx_mld_mac_ctxt_cmd(struct model_softc *sc, struct model_node *in, uint32_t action, int assoc)
{
	(void)sc; (void)in; (void)action; (void)assoc;
	actions++;
	return 0;
}
static int
iwx_lookup_cmd_ver(struct model_softc *sc, unsigned group, unsigned cmd)
{
	(void)sc; (void)group; (void)cmd;
	return 2;
}
static int
iwx_mimo_enabled(struct model_softc *sc)
{
	(void)sc;
	return 0;
}
#include "native-txop-intel.inc"

int
main(void)
{
	struct model_softc sc;
	struct model_node node;
	struct model_phy phy = {0};
	static const uint16_t values[] = {0, 255, 256, 2047, 2048, 32767, 32768, 65535};
	size_t i;
	int ac;
	memset(&node, 0, sizeof(node));
	node.in_phyctxt = &phy;
	for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		memset(&sc, 0, sizeof(sc));
		sc.sc_ic.ic_bss = &node.in_ni;
		sc.sc_flags = IWX_FLAG_MAC_ACTIVE;
		for (ac = 0; ac < 4; ac++) {
			struct wmeParams param = {0, 2, 3, 4, values[i], 0};
			sc.sc_ic.ic_wme.wme_params[ac] = param;
			sc.sc_ic.ic_wme.wme_chanParams.cap_wmeParams[ac] = param;
		}
		actions = 0;
		CHECK(iwm_mac_ctxt_cmd(&sc, &node, IWM_FW_CTXT_ACTION_MODIFY, 0) ==
		    (values[i] > 2047 ? EOPNOTSUPP : 0));
		CHECK(actions == (values[i] > 2047 ? 0U : 1U));
		if (values[i] <= 2047)
			CHECK(htole16(saved_mac.ac[3].edca_txop) == values[i] * 32U);
		actions = 0;
		CHECK(iwx_mac_ctxt_cmd(&sc, &node, IWX_FW_CTXT_ACTION_MODIFY, 0) ==
		    (values[i] > 2047 ? EOPNOTSUPP : 0));
		CHECK(actions == (values[i] > 2047 ? 0U : 1U));
		if (values[i] <= 2047)
			CHECK(htole16(saved_mac.ac[3].edca_txop) == values[i] * 32U);
		actions = 0;
		CHECK(iwx_mld_add_sta_cmd(&sc, &node, 1) == (values[i] > 2047 ? EOPNOTSUPP : 0));
		CHECK(actions == (values[i] > 2047 ? 0U : 2U));
		if (values[i] <= 2047)
			CHECK(htole16(saved_link.ac[3].edca_txop) == values[i] * 32U);
		/* No QoS payload: unsupported EDCA cannot block teardown/initial link. */
		actions = 0;
		CHECK(iwm_mac_ctxt_cmd(&sc, &node, IWM_FW_CTXT_ACTION_REMOVE, 0) == 0);
		CHECK(actions == 1);
		CHECK(saved_mac.ac[3].edca_txop == 0);
		actions = 0;
		CHECK(iwx_mac_ctxt_cmd(&sc, &node, IWX_FW_CTXT_ACTION_REMOVE, 0) == 0);
		CHECK(actions == 1);
		CHECK(saved_mac.ac[3].edca_txop == 0);
		actions = 0;
		CHECK(iwx_mld_add_sta_cmd(&sc, &node, 0) == 0);
		CHECK(actions == 3);
		CHECK(saved_link.ac[3].edca_txop == 0);
		actions = 0;
		CHECK(iwx_mld_rm_sta_cmd(&sc, &node) == 0);
		CHECK(actions == 3);
	}
	/* Reject an invalid value in every AC before any command is sent. */
	for (ac = 0; ac < 4; ac++) {
		int other;
		for (other = 0; other < 4; other++) {
			sc.sc_ic.ic_wme.wme_params[other].wmep_txopLimit = 0;
			sc.sc_ic.ic_wme.wme_chanParams.cap_wmeParams[other].wmep_txopLimit = 0;
		}
		sc.sc_ic.ic_wme.wme_params[ac].wmep_txopLimit = values[4];
		sc.sc_ic.ic_wme.wme_chanParams.cap_wmeParams[ac].wmep_txopLimit = values[4];
		actions = 0;
		CHECK(iwm_mac_ctxt_cmd(&sc, &node, IWM_FW_CTXT_ACTION_ADD, 0) == EOPNOTSUPP);
		CHECK(actions == 0);
		sc.sc_flags = 0;
		CHECK(iwx_mac_ctxt_cmd(&sc, &node, IWX_FW_CTXT_ACTION_ADD, 0) == EOPNOTSUPP);
		CHECK(actions == 0);
		CHECK(iwx_mld_add_sta_cmd(&sc, &node, 1) == EOPNOTSUPP);
		CHECK(actions == 0);
		/* The monitor MAC and MLD MAC commands contain no EDCA payload. */
		sc.sc_ic.ic_opmode = IEEE80211_M_MONITOR;
		CHECK(iwx_mac_ctxt_cmd(&sc, &node, IWX_FW_CTXT_ACTION_ADD, 0) == 0);
		CHECK(actions == 1);
		CHECK(saved_mac.ac[3].edca_txop == 0);
		sc.sc_ic.ic_opmode = IEEE80211_M_STA;
		sc.sc_use_mld_api = 1;
		actions = 0;
		CHECK(iwx_mac_ctxt_cmd(&sc, &node, IWX_FW_CTXT_ACTION_ADD, 0) == 0);
		CHECK(actions == 1);
		sc.sc_use_mld_api = 0;
	}
	printf("TXOP Intel contexts: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}
