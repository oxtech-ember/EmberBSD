/* SPDX-License-Identifier: BSD-2-Clause */
/* Fixtures for current BSD station callbacks extracted by bwfm-sta.sh. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IEEE80211_ADDR_LEN 6
#define ETH_ALEN 6
#define WLAN_ACTION_WNM 10
#define WLAN_ACTION_RADIO_MEASUREMENT 5
#define WNM_BSS_TRANS_MGMT_QUERY 6
#define WNM_BSS_TRANS_MGMT_REQ 7
#define WNM_BSS_TRANS_MGMT_RESP 8
#define WLAN_RRM_RADIO_MEASUREMENT_REQUEST 0
#define WLAN_RRM_RADIO_MEASUREMENT_REPORT 1
#define WLAN_RRM_NEIGHBOR_REPORT_REQUEST 4
#define WLAN_RRM_NEIGHBOR_REPORT_RESPONSE 5
#define WLAN_FC_TYPE_MGMT 0
#define WLAN_FC_STYPE_ACTION 13
#define os_memset memset
#define os_memcpy memcpy
#define os_memcmp memcmp
#define os_free free
#define MSG_DEBUG 0
#define wpa_printf(level, ...) do { (void)(level); \
    (void)snprintf(debug_log, sizeof(debug_log), __VA_ARGS__); } while (0)
#define ELOOP_ALL_CTX ((void *)-1)
#include "abi.h"
#include "routes.h"
typedef uint8_t u8;
enum { EVENT_SCAN_RESULTS, EVENT_TX_STATUS, EVENT_DISASSOC, EVENT_RX_MGMT };
struct os_reltime { unsigned sec, usec; };
struct bsd_driver_data {
    void *ctx;
    int sta_btm, sta_rrm, sta_scan_pending;
    uintptr_t sta_scan_generation;
    uint32_t sta_scan_cookie, sta_scan_epoch, sta_cookie, sta_tx_generation;
    uint16_t sta_tx_freq;
    uint8_t sta_addr[6], sta_scan_bssid[6];
    uint8_t *sta_tx_frame;
    size_t sta_tx_len;
    struct os_reltime sta_scan_completed;
};
union wpa_event_data {
    struct { int aborted; } scan_info;
    struct { int type, stype, ack, link_id; const u8 *dst, *data; size_t data_len; } tx_status;
    struct { const u8 *frame; size_t frame_len; unsigned freq; int link_id; } rx_mgmt;
};
static struct ieee80211req_sta_mgmt current, cancelled;
static unsigned checks, failures, rx, tx, scans, disconnects, cancellations;
static int info_error, last_ack, last_aborted;
static char debug_log[256];
static void check(bool pass, const char *why)
{ checks++; if (!pass) { fprintf(stderr,"FAIL: %s\n",why); failures++; } }
static void eloop_cancel_timeout(void (*fn)(void *,void *), void *a, void *b)
{ (void)fn; (void)a; (void)b; }
static void os_get_reltime(struct os_reltime *time) { time->sec = 1; time->usec = 0; }
static int bsd_sta_info(struct bsd_driver_data *drv, struct ieee80211req_sta_mgmt *info)
{ (void)drv; *info = current; return info_error; }
static int bsd_sta_set(struct bsd_driver_data *drv, const struct ieee80211req_sta_mgmt *req, unsigned op)
{
    (void)drv; cancelled = *req; cancelled.op = (uint16_t)op;
    cancellations++; return 0;
}
static void wpa_supplicant_event(void *ctx, int event, union wpa_event_data *data)
{
    (void)ctx;
    switch (event) {
    case EVENT_RX_MGMT:
        check(data->rx_mgmt.frame_len >= 27 && data->rx_mgmt.freq == current.freq,
            "RX event includes action header and current frequency");
        check(data->rx_mgmt.frame[0] == 0xd0 &&
            memcmp(data->rx_mgmt.frame+10,current.bssid,6) == 0 &&
            memcmp(data->rx_mgmt.frame+16,current.bssid,6) == 0,
            "RX frame identifies the current AP as source and BSSID");
        rx++; break;
    case EVENT_TX_STATUS:
        check(data->tx_status.type == WLAN_FC_TYPE_MGMT &&
            data->tx_status.stype == WLAN_FC_STYPE_ACTION &&
            data->tx_status.data_len >= 27 && data->tx_status.data != NULL,
            "TX completion retains frame through event dispatch");
        tx++; last_ack = data->tx_status.ack; break;
    case EVENT_SCAN_RESULTS:
        scans++; last_aborted = data->scan_info.aborted; break;
    case EVENT_DISASSOC:
        disconnects++; break;
    default: abort();
    }
}
static void bsd_sta_scan_timeout(void *,void *);
static void bsd_sta_tx_timeout(void *,void *);
static void
#include "bsd_sta_scan_clear.h"
static void
#include "bsd_sta_scan_timeout.h"
static void
#include "bsd_sta_tx_status.h"
static void
#include "bsd_sta_disconnected.h"
static void
#include "bsd_sta_tx_timeout.h"
static int
#include "bsd_sta_tx_body.h"
static int
#include "bsd_sta_rx_body.h"
static void
#include "bsd_sta_event.h"
static void fixture(struct bsd_driver_data *drv)
{
    memset(drv,0,sizeof(*drv)); memset(&current,0,sizeof(current));
    memset(&cancelled,0,sizeof(cancelled));
    current.version = IEEE80211_STA_VERSION; current.generation = 40;
    current.freq = 5300; current.bssid[0] = 2; current.flags = IEEE80211_STA_ASSOCIATED;
    drv->sta_btm = drv->sta_rrm = 1; drv->sta_cookie = 42;
    drv->sta_tx_generation = current.generation; drv->sta_tx_freq = current.freq;
    drv->sta_scan_cookie = 50; drv->sta_scan_epoch = current.generation;
    drv->sta_scan_generation = 100;
    memcpy(drv->sta_scan_bssid,current.bssid,6);
    rx = tx = scans = disconnects = cancellations = 0; info_error = 0;
    last_ack = last_aborted = 0;
    debug_log[0] = '\0';
}
static struct ieee80211req_sta_mgmt event_request(void)
{
    struct ieee80211req_sta_mgmt req = current;
    req.flags = 0; req.cookie = 0; req.op = IEEE80211_STA_GET;
    req.len = 7; req.data[0] = WLAN_ACTION_WNM;
    req.data[1] = WNM_BSS_TRANS_MGMT_REQ; req.data[2] = 1;
    return req;
}
static void pending_tx(struct bsd_driver_data *drv)
{
    drv->sta_tx_len = 29; drv->sta_tx_frame = calloc(1,29);
    if (drv->sta_tx_frame == NULL) abort();
    memcpy(drv->sta_tx_frame+4,current.bssid,6);
    drv->sta_tx_frame[24] = WLAN_ACTION_WNM;
    drv->sta_tx_frame[25] = WNM_BSS_TRANS_MGMT_RESP;
    drv->sta_tx_frame[26] = 7;
}
int main(void)
{
    struct bsd_driver_data drv;
    struct ieee80211req_sta_mgmt req;
    size_t i, header = offsetof(struct ieee80211req_sta_mgmt,data);
    fixture(&drv); req = event_request();
    bsd_sta_event(&drv,RTM_IEEE80211_STA_RX,&req,header+req.len);
    check(rx == 1,"valid BTM request reaches supplicant");
    for (i = 0; i < header+7; i++) {
        bsd_sta_event(&drv,RTM_IEEE80211_STA_RX,&req,i);
        check(rx == 1,"truncated route message is rejected");
    }
    for (i = 0; i < 12; i++) {
        fixture(&drv); req = event_request();
        switch (i) {
        case 0: req.version--; break;
        case 1: req.reserved = 1; break;
        case 2: req.len = 1025; break;
        case 3: req.generation--; break;
        case 4: req.freq++; break;
        case 5: req.bssid[5]++; break;
        case 6: req.flags = 1; break;
        case 7: req.data[0] = 0; break;
        case 8: req.data[1] = WNM_BSS_TRANS_MGMT_RESP; break;
        case 9: current.flags = 0; break;
        case 10: info_error = -1; break;
        case 11: drv.sta_btm = 0; break;
        }
        bsd_sta_event(&drv,RTM_IEEE80211_STA_RX,&req,header+7);
        check(rx == 0,"malformed, stale, foreign or disabled BTM event is rejected");
    }
    fixture(&drv); req = event_request(); req.data[0] = WLAN_ACTION_RADIO_MEASUREMENT;
    req.data[1] = WLAN_RRM_NEIGHBOR_REPORT_RESPONSE; req.len = 3;
    bsd_sta_event(&drv,RTM_IEEE80211_STA_RX,&req,header+3);
    check(rx == 1,"Neighbor Report response reaches supplicant");
    drv.sta_rrm = 0;
    bsd_sta_event(&drv,RTM_IEEE80211_STA_RX,&req,header+3);
    check(rx == 1,"disabled RRM cannot inject a response");
    fixture(&drv); req = event_request(); req.data[1] = WNM_BSS_TRANS_MGMT_QUERY; req.len = 4;
    check(bsd_sta_tx_body(&drv,req.data,req.len),"BTM query supported");
    req.data[0] = WLAN_ACTION_RADIO_MEASUREMENT; req.data[1] = WLAN_RRM_NEIGHBOR_REPORT_REQUEST;
    check(bsd_sta_tx_body(&drv,req.data,3) && !bsd_sta_tx_body(&drv,req.data,2),"Neighbor Report request needs token");
    for (i = 0; i < 9; i++) {
        fixture(&drv); pending_tx(&drv); req = event_request(); req.len = 0; req.cookie = 42; req.op = IEEE80211_STA_TX;
        req.flags = IEEE80211_STA_TX_ACK;
        switch (i) {
        case 0: req.cookie--; break;
        case 1: req.generation--; break;
        case 2: req.bssid[5]++; break;
        case 3: req.freq++; break;
        case 4: req.op = IEEE80211_STA_SCAN; break;
        case 5: req.flags |= IEEE80211_STA_SCAN_ABORTED; break;
        case 6: req.len = 1; break;
        case 7: drv.sta_tx_generation--; break;
        case 8: drv.sta_tx_freq++; break;
        }
        bsd_sta_event(&drv,RTM_IEEE80211_STA_TX,&req,header+req.len);
        check(tx == 0 && drv.sta_tx_frame != NULL,"TX completion must match cookie, epoch, peer, frequency and shape");
        bsd_sta_tx_status(&drv,0,0);
    }
    fixture(&drv); pending_tx(&drv); req = event_request(); req.len = 0; req.cookie = 42; req.op = IEEE80211_STA_TX; req.flags = IEEE80211_STA_TX_ACK;
    bsd_sta_event(&drv,RTM_IEEE80211_STA_TX,&req,header);
    check(tx == 1 && last_ack && drv.sta_tx_frame == NULL,"matching TX completion notifies ACK and releases frame");
    check(strstr(debug_log,"cookie=42 generation=40 freq=5300 category=10 action=8 token=7 ack=1") != NULL,
        "TX diagnostic identifies the acknowledged action without its payload");
    bsd_sta_event(&drv,RTM_IEEE80211_STA_TX,&req,header); check(tx == 1,"duplicate TX event ignored");
    fixture(&drv); pending_tx(&drv); bsd_sta_tx_timeout(&drv,(void *)(uintptr_t)41);
    check(tx == 0 && drv.sta_tx_frame != NULL,"old TX timeout cannot cancel a replacement");
    bsd_sta_tx_timeout(&drv,(void *)(uintptr_t)42);
    check(tx == 1 && !last_ack && cancellations == 1 && cancelled.cookie == 42 &&
        cancelled.generation == 40 && cancelled.op == IEEE80211_STA_CANCEL_TX,
        "TX timeout cancels original epoch and cookie and reports failure");
    check(strstr(debug_log,"token=7 ack=0") != NULL,
        "TX diagnostic distinguishes timeout from ACK");
    for (i = 0; i < 7; i++) {
        fixture(&drv); drv.sta_scan_pending = 1; req = event_request();
        req.len = 0; req.op = IEEE80211_STA_SCAN; req.cookie = drv.sta_scan_cookie;
        switch (i) {
        case 0: req.cookie--; break;
        case 1: req.generation--; break;
        case 2: req.bssid[5]++; break;
        case 3: req.op = IEEE80211_STA_TX; break;
        case 4: req.flags = IEEE80211_STA_TX_ACK; break;
        case 5: req.len = 1; break;
        case 6: drv.sta_scan_epoch--; break;
        }
        bsd_sta_event(&drv,RTM_IEEE80211_STA_SCAN,&req,header+req.len);
        check(scans == 0 && drv.sta_scan_pending,"old or malformed scan completion cannot release current scan work");
    }
    fixture(&drv); drv.sta_scan_pending = 1; req = event_request();
    req.len = 0; req.op = IEEE80211_STA_SCAN; req.cookie = drv.sta_scan_cookie;
    bsd_sta_event(&drv,RTM_IEEE80211_STA_SCAN,&req,header);
    check(scans == 1 && !last_aborted && !drv.sta_scan_pending,"matching successful scan releases radio work");
    bsd_sta_event(&drv,RTM_IEEE80211_STA_SCAN,&req,header); check(scans == 1,"duplicate scan completion ignored");
    fixture(&drv); drv.sta_scan_pending = 1;
    bsd_sta_scan_timeout(&drv,(void *)(uintptr_t)99);
    check(scans == 0 && drv.sta_scan_pending,"old timeout cannot cancel replacement scan");
    bsd_sta_scan_timeout(&drv,(void *)(uintptr_t)100);
    check(scans == 1 && last_aborted && !drv.sta_scan_pending && cancelled.cookie == 50 &&
        cancelled.generation == 40 && cancelled.op == IEEE80211_STA_CANCEL_SCAN,
        "current scan timeout cancels exact operation and emits aborted result");
    bsd_sta_scan_timeout(&drv,(void *)(uintptr_t)100); check(scans == 1,"expired timeout cannot complete twice");
    fixture(&drv); drv.sta_scan_pending = 1; pending_tx(&drv);
    bsd_sta_disconnected(&drv);
    check(disconnects == 1 && scans == 1 && last_aborted && tx == 0 && drv.sta_tx_frame == NULL,
        "disconnect silently frees pending TX and releases old scan work after disassociation");
    bsd_sta_scan_timeout(&drv,(void *)(uintptr_t)100);
    check(scans == 1,"disconnect invalidates queued timeout");
    printf("%s: %u production BSD station callback assertions\n",failures ? "FAIL" : "PASS",checks);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
