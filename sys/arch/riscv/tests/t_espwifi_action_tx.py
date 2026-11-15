#!/usr/bin/env python3
"""Exercise actual vendor Action TX wrappers and request ownership."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--cflags', default='', help='optional host compile flags')
p.add_argument('--analyze', action='store_true')
p.add_argument('--cppcheck')
a = p.parse_args()
arch = Path(__file__).resolve().parents[1]
wifi = arch / 'espressif/wifi'
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"

int espwifi_vendor_action_tx_submit(const void *, unsigned long, uint32_t,
    uint32_t, const unsigned char *);
int __wrap_esp_wifi_send_mgmt_frm_internal(const void *);
int __wrap_ieee80211_ioctl(void *);
int __wrap_wifi_send_mgmt_frame(void *);
int __wrap_ieee80211_mgmt_output(void *, void *, unsigned int);
void __wrap_ieee80211_tx_mgt_cb(void *);

static const int task_token;
static int mode, real_callbacks, events, frees;
static int tx_return, output_return, output_calls, ioctl_calls, send_calls;
static int observed_error;
static char mbuf_target, mbuf_other;
static unsigned char request1[64], request2[64];
static const uint8_t peer[6] = { 2, 1, 2, 3, 4, 5 };
static pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t output_cv = PTHREAD_COND_INITIALIZER;
static pthread_t output_thread;
static int output_started, allow_output_return;
static pthread_t reentry_thread;
static int reenter_on_done, block_next_send, real_send_entered;
static int allow_real_send, premature_free;
static unsigned char *tracked_request;

static void *output_task(void *arg);
static void *reentry_task(void *arg);

unsigned int espwifi_os_critical_enter(void) { return 0; }
void espwifi_os_critical_exit(unsigned int s) { (void)s; }
void *espwifi_os_task_current(void) { return (void *)&task_token; }
void espwifi_os_free(void *p) {
    const unsigned char *bytes = p;
    for (unsigned i = 0; i < 64; i++) assert(bytes[i] == 0);
    frees++;
}
void espwifi_if_action_tx_done(uint32_t c, uint32_t g, const uint8_t *b)
{
    assert(c == 17 && g == 23 && memcmp(b, peer, 6) == 0);
    events++;
    if (reenter_on_done) {
        reenter_on_done = 0;
        assert(pthread_create(&reentry_thread, NULL, reentry_task,
            request1) == 0);
        pthread_mutex_lock(&output_lock);
        while (!real_send_entered)
            pthread_cond_wait(&output_cv, &output_lock);
        pthread_mutex_unlock(&output_lock);
    }
}

int __real_esp_wifi_send_mgmt_frm_internal(const void *request)
{
    struct command { uint32_t magic; void *fn; char data[32]; } command;
    if (mode == 10)
        return ESP_OK; /* Success without an observed buffer is ambiguous. */
    if (mode == 11) {
        assert(pthread_create(&output_thread, NULL, output_task,
            (void *)request) == 0);
        pthread_mutex_lock(&output_lock);
        while (!output_started)
            pthread_cond_wait(&output_cv, &output_lock);
        pthread_mutex_unlock(&output_lock);
        return ESP_FAIL; /* Outer return races a still-running output call. */
    }
    if (block_next_send) {
        pthread_mutex_lock(&output_lock);
        real_send_entered = 1;
        pthread_cond_broadcast(&output_cv);
        while (!allow_real_send)
            pthread_cond_wait(&output_cv, &output_lock);
        block_next_send = 0;
        pthread_mutex_unlock(&output_lock);
    }
    if (mode == 7)
        return ESP_FAIL; /* Accepted API operation, failure before any buffer. */
    command.magic = 0x52;
    command.fn = NULL;
    ioctl_calls++;
    int error = __wrap_ieee80211_ioctl(&command);
    if (error != ESP_OK)
        return error;
    (void)request;
    return tx_return;
}
int __real_ieee80211_ioctl(void *request)
{
    if (((struct command { uint32_t magic; void *fn; char data[32]; } *)request)->magic != 0x52)
        return ESP_FAIL;
    return __wrap_wifi_send_mgmt_frame(request);
}
int __real_wifi_send_mgmt_frame(void *request)
{
	int error;

    (void)request;
    send_calls++;
    error = __wrap_ieee80211_mgmt_output(NULL, &mbuf_target,
        mode == 6 ? 4U : (13U << 4));
    if (tracked_request != NULL && tracked_request[0] == 0)
        premature_free = 1;
    if (mode == 12) {
        output_return = ESP_OK;
        (void)__wrap_ieee80211_mgmt_output(NULL, &mbuf_target, 13U << 4);
    }
    return error;
}
int __real_ieee80211_mgmt_output(void *node, void *mbuf, unsigned int subtype)
{
    (void)node; (void)mbuf; (void)subtype;
    output_calls++;
    if (mode == 11) {
        pthread_mutex_lock(&output_lock);
        output_started = 1;
        pthread_cond_broadcast(&output_cv);
        while (!allow_output_return)
            pthread_cond_wait(&output_cv, &output_lock);
        pthread_mutex_unlock(&output_lock);
    }
    if (mode == 1)
        __wrap_ieee80211_tx_mgt_cb(&mbuf_target);
    if (mode == 2) {
        __wrap_ieee80211_tx_mgt_cb(&mbuf_target);
        __wrap_ieee80211_tx_mgt_cb(&mbuf_target);
    }
    if (mode == 3)
        __wrap_ieee80211_tx_mgt_cb(&mbuf_other);
    return output_return;
}
void __real_ieee80211_tx_mgt_cb(void *mbuf)
{
    (void)mbuf;
    real_callbacks++;
}

static void *output_task(void *arg)
{
    struct command { uint32_t magic; void *fn; char data[32]; } command;
    (void)arg;
    command.magic = 0x52;
    command.fn = NULL;
    (void)__wrap_ieee80211_ioctl(&command);
    return NULL;
}

static void *reentry_task(void *arg)
{
    unsigned char *request = arg;

    memset(request, 0xa5, 64); /* Simulate allocator reusing the same address. */
    tracked_request = request;
    mode = 1;
    block_next_send = 1;
    assert(espwifi_vendor_action_tx_submit(request, 64, 17, 23, peer) == ESP_OK);
    return NULL;
}

static void run_one(const char *scenario)
{
    unsigned char *request = request1;
    int result;

    if (strcmp(scenario, "sync") == 0) mode = 1;
    else if (strcmp(scenario, "duplicate") == 0) mode = 2;
    else if (strcmp(scenario, "unrelated") == 0) mode = 3;
    else if (strcmp(scenario, "async") == 0) mode = 8;
    else if (strcmp(scenario, "outer-error-late") == 0) {
        mode = 5; tx_return = ESP_FAIL;
    } else if (strcmp(scenario, "output-fail") == 0) {
        mode = 9; output_return = ESP_FAIL;
    } else if (strcmp(scenario, "early-fail") == 0) mode = 7;
    else if (strcmp(scenario, "wrong-subtype") == 0) mode = 6;
    else if (strcmp(scenario, "success-unobserved") == 0) mode = 10;
    else if (strcmp(scenario, "output-inflight") == 0) mode = 11;
    else if (strcmp(scenario, "retry-output") == 0) {
        mode = 12; output_return = ESP_FAIL;
    }
    else if (strcmp(scenario, "same-address-reentry") == 0) {
        mode = 7; reenter_on_done = 1;
    }
    else if (strcmp(scenario, "busy-sequential") == 0) mode = 8;
    else assert(0);

    result = espwifi_vendor_action_tx_submit(request, sizeof(request1),
        17, 23, peer);
    assert(result == ESP_OK); /* Ownership was accepted despite send failure. */
    if (mode == 10 || mode == 6) {
        assert(events == 0 && frees == 0);
        if (mode == 6)
            assert(output_calls == 1 && real_callbacks == 0);
        return;
    }
    if (mode == 12)
        assert(output_calls == 2);
    if (mode == 11) {
        assert(events == 0 && frees == 0 && output_started);
        pthread_mutex_lock(&output_lock);
        allow_output_return = 1;
        pthread_cond_broadcast(&output_cv);
        pthread_mutex_unlock(&output_lock);
        assert(pthread_join(output_thread, NULL) == 0);
        assert(events == 0 && frees == 0);
        __wrap_ieee80211_tx_mgt_cb(&mbuf_target);
        assert(events == 1 && frees == 1);
        return;
    }
    if (strcmp(scenario, "same-address-reentry") == 0) {
        assert(events == 1 && frees == 1);
        pthread_mutex_lock(&output_lock);
        allow_real_send = 1;
        pthread_cond_broadcast(&output_cv);
        pthread_mutex_unlock(&output_lock);
        assert(pthread_join(reentry_thread, NULL) == 0);
        assert(events == 2 && frees == 2 && premature_free == 0);
        return;
    }
    if (strcmp(scenario, "async") == 0 ||
        strcmp(scenario, "outer-error-late") == 0 ||
        strcmp(scenario, "unrelated") == 0 ||
        strcmp(scenario, "busy-sequential") == 0) {
        assert(events == 0 && frees == 0);
        if (strcmp(scenario, "busy-sequential") == 0) {
            assert(espwifi_vendor_action_tx_submit(request2, sizeof(request2),
                17, 23, peer) == ESP_ERR_INVALID_STATE);
            assert(frees == 0 && events == 0);
        }
        if (strcmp(scenario, "unrelated") == 0)
            assert(real_callbacks == 1 && events == 0);
        if (strcmp(scenario, "outer-error-late") == 0)
            assert(tx_return == ESP_FAIL && output_calls == 1);
        __wrap_ieee80211_tx_mgt_cb(&mbuf_target);
    }
    if (strcmp(scenario, "duplicate") == 0)
        assert(real_callbacks == 2);
    if (strcmp(scenario, "unrelated") == 0)
        assert(real_callbacks == 2);
    if (strcmp(scenario, "early-fail") == 0)
        assert(ioctl_calls == 0 && output_calls == 0 && send_calls == 0);
    assert(events == 1 && frees == 1);

    if (strcmp(scenario, "busy-sequential") == 0) {
        memset(request2, 0x5a, sizeof(request2));
        mode = 1;
        assert(espwifi_vendor_action_tx_submit(request2, sizeof(request2),
            17, 23, peer) == ESP_OK);
        assert(events == 2 && frees == 2);
    }
    (void)observed_error;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    memset(request1, 0xa5, sizeof(request1));
    run_one(argv[1]);
    puts("Action TX ownership scenario passed");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    tmp = Path(td)
    (tmp / 'esp_err.h').write_text('''#define ESP_OK 0\n#define ESP_FAIL 0x101\n#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n''')
    cfile = tmp / 'fixture.c'
    cfile.write_text(source)
    binary = tmp / 'fixture'
    subprocess.run([a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
        *shlex.split(a.cflags), '-I', str(tmp), '-I', str(arch / 'espressif'),
        str(wifi / 'action_tx.c'), str(cfile), '-o', str(binary)], check=True)
    if a.analyze:
        subprocess.run([a.cc, '--analyze', '-std=c11', '-I', str(tmp),
            '-I', str(arch / 'espressif'), str(wifi / 'action_tx.c')], check=True)
    if a.cppcheck:
        subprocess.run([a.cppcheck, '--enable=warning,performance,portability',
            '--suppress=missingIncludeSystem', '--error-exitcode=1', '--std=c11',
            '-I'+str(tmp), '-I'+str(arch/'espressif'), str(wifi/'action_tx.c')], check=True)
    for scenario in ('sync', 'duplicate', 'unrelated', 'async',
                     'outer-error-late', 'output-fail', 'early-fail',
                     'wrong-subtype', 'success-unobserved', 'output-inflight',
                     'retry-output', 'same-address-reentry',
                     'busy-sequential'):
        subprocess.run([str(binary), scenario], check=True)
print('vendor Action TX ownership and drain scenarios passed')
