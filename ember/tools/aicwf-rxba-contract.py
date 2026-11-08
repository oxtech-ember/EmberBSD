#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Extract complete AIC RX and lifetime consumers beside the shared RX BA unit."""
import pathlib
import os
import re
import sys

src, out = map(pathlib.Path, sys.argv[1:])
driver = pathlib.Path(os.environ.get('AICWF_RXBA_SOURCE',
    src / 'sys/dev/sdmmc/if_aicwf.c')).read_text()
parts = []
for line in driver.splitlines():
    if re.match(r'^#define\s+AICWF_(RX|REORDER|KEY_|HWKEY|STA_NONE|CIPHER_CCMP|MM_KEY_|ME_SET_CONTROL_PORT|HDR_LEN|TYPE_CFG|TYPE_TX_CFM|TYPE_MSG)', line):
        parts.append(line)
for name in ('aicwf_rx_input', 'aicwf_rx_free', 'aicwf_rx_discard',
             'aicwf_rx_now', 'aicwf_rx_schedule_locked', 'aicwf_rx_fence_locked',
             'aicwf_rx_retire', 'aicwf_rx_begin', 'aicwf_rx_resume',
             'aicwf_rx_output', 'aicwf_rx_submit', 'aicwf_rxba_timeout',
             'aicwf_rx_ether', 'aicwf_amsdu_read', 'aicwf_rx_amsdu',
             'aicwf_rx_data', 'aicwf_rx', 'aicwf_key_set_cb',
             'aicwf_key_delete_cb', 'aicwf_key_task', 'aicwf_newstate',
             'aicwf_connect_ind', 'aicwf_disconnect_ind'):
    match = re.search(r'^static [^\n]+\n' + name + r'\([^;]*?\n\{.*?^\}',
                      driver, re.M | re.S)
    if not match:
        raise SystemExit('Missing complete AIC consumer: ' + name)
    parts.append(match[0])
(out / 'aicwf-rxba.inc').write_text('\n'.join(parts) + '\n')
