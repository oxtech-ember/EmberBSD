#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Compile the actual native SAE owner with bounded kernel service fixtures."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import re

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default=os.environ.get('CC', 'cc'))
p.add_argument('--cflags', default=os.environ.get('CFLAGS', ''))
p.add_argument('--build-dir', required=True, type=Path)
a = p.parse_args()
owner = Path(__file__).resolve().parent.parent
abi = (owner / 'ieee80211_ioctl.h').read_text()
start = abi.index('#define IEEE80211_IOC_SAE')
end = abi.index('\n};', abi.index('struct ieee80211req_sae {', start)) + 3
key_start = abi.index('struct ieee80211req_key {')
key_end = abi.index('\n};', abi.index('struct ieee80211req_del_key {', key_start)) + 3
source = (owner / 'ieee80211_sae.c').read_text()
source = source[source.index('enum sae_phase'):]
fixture = (owner / 'tests/native-sae-contract.c').read_text()
fixture = fixture.replace('/* ABI */', abi[key_start:key_end] + '\n' + abi[start:end])
fixture = fixture.replace('/* IMPLEMENTATION */', source)
input_source = (owner / 'ieee80211_input.c').read_text()
duplicate = re.search(r'(?m)^static bool\nieee80211_rx_duplicate\([\s\S]+?^\}', input_source).group(0)
fixture = fixture.replace('/* ACTUAL DUPLICATE */', duplicate)
amsdu = re.search(r'\tamsdu = ([\s\S]+?);', input_source).group(1)
fixture = fixture.replace('/* ACTUAL AMSDU SELECTION */',
    'static bool input_amsdu_selection(struct ieee80211com *ic, struct ieee80211_node *ni, struct mbuf *m) { '
    'const struct ieee80211_frame *wh=mtod(m,const void *); (void)ic; (void)ni; return ' + amsdu + '; }')


netbsd = (owner / 'ieee80211_netbsd.c').read_text()
allocator = re.search(r'(?m)^struct mbuf \*\nieee80211_getmgtframe\([\s\S]+?^\}', netbsd).group(0)
crypto = (owner / 'ieee80211_crypto.c').read_text()
encap = re.search(r'(?m)^struct ieee80211_key \*\nieee80211_crypto_encap\([\s\S]+?^\}', crypto).group(0)
mbuf = (owner.parent / 'sys/mbuf.h').read_text()
prepend = re.search(r'(?m)^#define M_PREPEND\([\s\S]+?^} while \(0\)', mbuf).group(0)
fixture = fixture.replace('/* ACTUAL TX ALLOCATION */', prepend + '\n' + allocator + '\n' + encap)

ioctl = (owner / 'ieee80211_ioctl.c').read_text()
fn = re.search(r'(?m)^int\nieee80211_sae_auth_frame_validate\([\s\S]+?^\}', ioctl).group(0)
fixture = fixture.replace('/* AUTH VALIDATOR */', fn)
node = (owner / 'ieee80211_node.c').read_text()
node_functions = '\n'.join(re.search(r'(?m)^void\n' + name + r'\([\s\S]+?^\}', node).group(0)
    for name in ('ieee80211_node_authorize', 'ieee80211_node_unauthorize'))
fixture = fixture.replace('/* NODE AUTHORIZATION */', node_functions)
a.build_dir.mkdir(parents=True, exist_ok=True)
check = a.build_dir / 'native-sae-contract.c'
check.write_text(fixture)
flags = shlex.split(a.cflags)
variants = [("", [])]
if '-DSAE_TEST_NO_ATOMIC64' not in flags:
    variants.append(("-noatomic64", ['-DSAE_TEST_NO_ATOMIC64']))
entries = []
commands = []
for suffix, extra in variants:
    output = a.build_dir / ('native-sae-contract' + suffix)
    cmd = shlex.split(a.cc) + ['-std=c99', '-Wall', '-Wextra', '-Werror'] + flags + extra
    cmd += ['-I' + str(owner), str(check), '-o', str(output)]
    entries.append({'directory': str(a.build_dir.resolve()),
                    'file': str(check.resolve()), 'arguments': cmd})
    commands.append((cmd, output))
(a.build_dir / 'compile_commands.json').write_text(json.dumps(entries, indent=2) + '\n')
for cmd, output in commands:
    subprocess.run(cmd, check=True)
    subprocess.run(shlex.split(os.environ.get("TEST_RUNNER", "")) +
                   [str(output.resolve())], check=True)
