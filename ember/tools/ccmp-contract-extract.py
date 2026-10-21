#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Extract actual source for the host CCMP consumer contract. AI-assisted.
import pathlib
import re
import sys

src, out = map(pathlib.Path, sys.argv[1:])

def read(name):
    return (src / name).read_text()

def notices(text):
    return text[:re.search(r'^#', text, re.M).start()]

def body(text, name):
    match = re.search(r'^' + re.escape(name) + r'\([^;]*?\n\{', text, re.M)
    if match is None:
        raise ValueError(name)
    end = text.index('\n}', match.end()) + 2
    return text[match.start():end] + '\n'

def structure(text, name):
    match = re.search(r'^struct ' + re.escape(name) + r' \{.*?^}[^;]*;', text, re.M | re.S)
    if match is None:
        raise ValueError(name)
    return match.group() + '\n'

def noincludes(text):
    return re.sub(r'^#include[^\n]*\n', '', text, flags=re.M)

crypto = read('sys/net80211/ieee80211_crypto.h')
(out/'native-key.inc').write_text(notices(crypto) + '\n' +
    structure(crypto, 'ieee80211_key') + structure(crypto, 'ieee80211_cipher'))
(out/'native-80211.inc').write_text(read('sys/net80211/ieee80211.h'))
ccmp = read('sys/net80211/ieee80211_crypto_ccmp.c')
# Keep the complete cipher, stopping before module registration only.
(out/'native-ccmp.inc').write_text(noincludes(ccmp[:ccmp.index('IEEE80211_CRYPTO_SETUP')]))
netbsd = read('sys/net80211/ieee80211_netbsd.c')
notify = notices(netbsd)
# The optional new helper remains absent in the scalar baseline for RED.
if re.search(r'^ieee80211_notify_replay_failure_rsc\(', netbsd, re.M):
    notify += '\nvoid\n' + body(netbsd, 'ieee80211_notify_replay_failure_rsc')
notify += '\nvoid\n' + body(netbsd, 'ieee80211_notify_replay_failure')
(out/'native-notify.inc').write_text(notify)
ioctl = read('sys/net80211/ieee80211_ioctl.c')
(out/'native-setkey.inc').write_text(notices(ioctl) + '\nstatic int\n' +
    body(ioctl, 'ieee80211_ioctl_setkey'))
(out/'native-crypto-decap.inc').write_text(notices(read('sys/net80211/ieee80211_crypto.c')) + '\nstruct ieee80211_key *\n' + body(read('sys/net80211/ieee80211_crypto.c'), 'ieee80211_crypto_decap'))
proto = read('sys/net80211/ieee80211_proto.h')
(out/'native-hdrsize.inc').write_text(notices(proto) + '\nstatic inline int\n' +
    body(proto, 'ieee80211_hdrsize'))
# Native AES/CCM structures and all BearSSL constant-time encryption source.
for name in ['aes.h','aes_bear.h','aes_ccm.h','aes_impl.h']:
    (out/name).write_text(noincludes(read('sys/crypto/aes/'+name)))
for name in ['aes_ct.c','aes_ct_enc.c']:
    text = noincludes(read('sys/crypto/aes/'+name))
    # CCM only uses the encryption schedule, not the unused CBC decrypt adapter.
    if name == 'aes_ct.c':
        text = text[:text.index('unsigned\nbr_aes_ct_keysched_stddec')]
    (out/(name+'.inc')).write_text(text)
bear = read('sys/crypto/aes/aes_bear.c')
functions = [('aesbear_setkey','void'),('aesbear_setenckey','void'),
    ('aesbear_enc','void'),('aesbear_cbcmac_update1','void'),
    ('aesbear_ccm_enc1','void'),('aesbear_ccm_dec1','void')]
(out/'native-aes.inc').write_text(notices(bear) + '\n'.join(
    'static '+result+'\n'+body(bear, name) for name,result in functions))
ccm = read('sys/crypto/aes/aes_ccm.c')
(out/'native-ccm.inc').write_text(noincludes(ccm[:ccm.index('/* XXX provisional hack */')]))
(out/'native-ccm-mbuf.inc').write_text(noincludes(read('sys/crypto/aes/aes_ccm_mbuf.c')))
# Preserve the actual user request layouts and replay event wire layout.
request = read('sys/net80211/ieee80211_ioctl.h')
(out/'native-request.inc').write_text(notices(request) +
    structure(request, 'ieee80211req_key') + structure(request, 'ieee80211req') +
    structure(read('sys/net80211/ieee80211_netbsd.h'), 'ieee80211_replay_event'))
