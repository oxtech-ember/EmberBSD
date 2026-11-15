#!/usr/bin/env python3
"""Compile and exercise the production STA Action lifetime helpers."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', default=os.environ.get('CC', 'cc'))
p.add_argument('--analyze', action='store_true')
p.add_argument('--cppcheck')
p.add_argument('--cflags', default=os.environ.get('STA_TEST_CFLAGS',
                                                  os.environ.get('CFLAGS', '')))
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
header = (root / 'ieee80211_sta_action.h').read_text()
ioctl = (root / 'ieee80211_ioctl.h').read_text()


def block(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 0
    for pos in range(brace, len(text)):
        if text[pos] == '{':
            depth += 1
        elif text[pos] == '}':
            depth -= 1
            if depth == 0:
                return text[start:pos + 1]
    raise AssertionError(marker)


def macro(text, name):
    match = re.search(r'^#define\s+' + re.escape(name) + r'\s+([^\n]+)',
                      text, re.M)
    assert match, name
    return match.group(1).split('/*', 1)[0].strip()


req = block(ioctl, 'struct ieee80211req_sta_mgmt {') + ';'
state = block(header, 'struct ieee80211_sta_action_state {') + ';'
funcs = '\n'.join(block(header, 'static inline ' + ret + '\nieee80211_sta_action_' + name)
    for name, ret in (('begin', 'int'), ('match', 'bool'), ('cancel', 'bool'),
                      ('invalidate', 'void'), ('report', 'bool'),
                      ('release', 'bool')))
consts = '\n'.join('#define %s %s' % (name, macro(ioctl, name)) for name in (
    'IEEE80211_STA_VERSION', 'IEEE80211_STA_MAX_BODY', 'IEEE80211_STA_TX',
    'IEEE80211_STA_CANCEL_TX', 'IEEE80211_STA_TX_ACK'))
code = r'''#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define IEEE80211_ADDR_LEN 6
#define CHECK(e) ((e) ? (void)0 : abort())
''' + consts + '\n' + req + '\n' + state + '\n' + funcs + r'''
static struct ieee80211req_sta_mgmt
request(uint32_t cookie, uint32_t generation, uint16_t freq, uint8_t tail)
{
	struct ieee80211req_sta_mgmt req = {0};
	req.version = IEEE80211_STA_VERSION;
	req.op = IEEE80211_STA_TX;
	req.cookie = cookie;
	req.generation = generation;
	req.freq = freq;
	req.len = 3;
	req.bssid[0] = 2;
	req.bssid[5] = tail;
	return req;
}

int
main(void)
{
	struct ieee80211_sta_action_state state = {0};
	struct ieee80211req_sta_mgmt first = request(1, 7, 2412, 9);
	struct ieee80211req_sta_mgmt notice = {0};
	struct ieee80211req_sta_mgmt changed;

	assert(ieee80211_sta_action_begin(&state, &first) == 0);
	assert(state.active && !state.cancelled && !state.reported);
	assert(ieee80211_sta_action_begin(&state, &first) == EBUSY);
	assert(ieee80211_sta_action_match(&state, &first));
	changed = first; changed.freq++;
	assert(!ieee80211_sta_action_match(&state, &changed));
	changed = first; changed.generation++;
	assert(!ieee80211_sta_action_match(&state, &changed));
	changed = first; changed.bssid[5]++;
	assert(!ieee80211_sta_action_match(&state, &changed));
	assert(!ieee80211_sta_action_report(&state, &changed, false, &notice));
	assert(!state.reported);

	/* Cancellation marks the logical tuple but never releases transport state. */
	changed = first;
	changed.op = IEEE80211_STA_CANCEL_TX;
	changed.freq = 0;
	changed.len = 0;
	assert(ieee80211_sta_action_cancel(&state, &changed));
	assert(state.active && state.cancelled);
	assert(!ieee80211_sta_action_report(&state, &first, true, &notice));
	assert(ieee80211_sta_action_release(&state, &first));
	assert(!state.active);

	/* A stale release cannot clear a new generation using the same interface. */
	struct ieee80211req_sta_mgmt second = request(2, 8, 2437, 10);
	assert(ieee80211_sta_action_begin(&state, &second) == 0);
	assert(!ieee80211_sta_action_release(&state, &first));
	assert(state.active && state.cookie == second.cookie &&
	    state.generation == second.generation);
	assert(ieee80211_sta_action_report(&state, &second, false, &notice));
	assert(notice.version == IEEE80211_STA_VERSION &&
	    notice.op == IEEE80211_STA_TX && notice.cookie == second.cookie &&
	    notice.generation == second.generation && notice.freq == second.freq &&
	    notice.flags == 0 && notice.len == 0 &&
	    memcmp(notice.bssid, second.bssid, IEEE80211_ADDR_LEN) == 0);
	assert(!ieee80211_sta_action_report(&state, &second, true, &notice));
	assert(ieee80211_sta_action_release(&state, &second));

	/* ACK is explicit; a TX-done path without peer ACK cannot set its flag. */
	struct ieee80211req_sta_mgmt third = request(3, 9, 2462, 11);
	assert(ieee80211_sta_action_begin(&state, &third) == 0);
	assert(ieee80211_sta_action_report(&state, &third, true, &notice));
	assert(notice.flags == IEEE80211_STA_TX_ACK);
	assert(ieee80211_sta_action_release(&state, &third));

	/* Invalid tuple forms and stale cancel do not reserve or cancel state. */
	changed = request(4, 10, 2412, 12); changed.bssid[0] = 1;
	assert(ieee80211_sta_action_begin(&state, &changed) == EINVAL);
	changed = request(4, 10, 2412, 12); changed.cookie = 0;
	assert(ieee80211_sta_action_begin(&state, &changed) == EINVAL);
	assert(ieee80211_sta_action_begin(&state, &first) == 0);
	changed = first; changed.op = IEEE80211_STA_CANCEL_TX;
	changed.len = 0; changed.cookie++;
	assert(!ieee80211_sta_action_cancel(&state, &changed));
	assert(!state.cancelled);
	ieee80211_sta_action_invalidate(&state);
	assert(state.active && state.cancelled);
	assert(!ieee80211_sta_action_report(&state, &first, true, &notice));
	assert(ieee80211_sta_action_release(&state, &first));
	return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'sta-action.c'
    out = Path(td) / 'sta-action'
    c.write_text(code.replace('assert(', 'CHECK('))
    if a.cppcheck:
        subprocess.run([a.cppcheck, '--enable=warning,performance,portability',
            '--suppress=missingIncludeSystem',
            '--error-exitcode=1',
            '--std=c11', str(c)], check=True)
    if a.analyze:
        subprocess.run([a.cc, '--analyze', '-std=c11', '-Wall', '-Wextra',
            '-Werror', str(c), '-o', str(Path(td) / 'analysis.plist')], check=True)
    subprocess.run([a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(a.cflags), str(c), '-o', str(out)], check=True)
    runner = os.environ.get('TEST_RUNNER')
    if runner:
        subprocess.run([runner, str(out)], check=True)
    else:
        subprocess.run([str(out)], check=True)
print('net80211 STA Action lifecycle helper fixture passed')
