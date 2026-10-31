#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the native wpa_supplicant profile without building it."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bmake', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()
directory = Path(__file__).resolve().parents[1] / 'bin/wpa_supplicant'
command = [str(args.bmake.resolve(strict=True)), '-C', str(directory),
           'EMBER_WPA_SOURCE=' + str(args.source.resolve(strict=True))]
env = os.environ.copy()
for key in ('EMBER_WPA_PROFILE', 'NO_CRYPT', 'NO_OPENSSL', 'RELEASE_CRUNCH',
            'NO_ENABLE_WPA_SUPPLICANT_EAPOL'):
    env.pop(key, None)


def query(*assignments):
    return subprocess.run(command + list(assignments) +
                          ['-V', '${SRCS}', '-V', '${CPPFLAGS}', '-V', '${LDADD}'],
                          env=env, capture_output=True, text=True)


def require(condition, message):
    if not condition:
        raise SystemExit(message)


full = query()
require(full.returncode == 0, full.stderr)
full_src, full_cpp, full_libs = map(str.split, full.stdout.splitlines())
require({'eap.c', 'eap_tls.c', 'tls_openssl.c', 'sae.c', 'sme.c'} <= set(full_src),
        'default profile lost EAP/TLS or SAE')
require({'-DIEEE8021X_EAPOL', '-DEAP_TLS', '-DCONFIG_SAE', '-DCONFIG_SME'} <= set(full_cpp),
        'default profile flags changed')
require({'-lssl', '-lcrypto', '-ldes'} <= set(full_libs), 'default libraries changed')

personal = query('EMBER_WPA_PROFILE=personal')
require(personal.returncode == 0, personal.stderr)
src, cpp, libs = map(str.split, personal.stdout.splitlines())
require({'sae.c', 'dragonfly.c', 'sme.c', 'wpa.c', 'wpa_ie.c', 'crypto_openssl.c'} <= set(src),
        'personal profile lost SAE, WPA or OpenSSL')
require(not {'eap.c', 'eap_methods.c', 'eapol_supp_sm.c', 'eap_tls.c', 'tls_openssl.c'} & set(src),
        'personal profile includes an enterprise EAP or TLS source')
require({'-DCONFIG_SAE', '-DCONFIG_SME', '-DCONFIG_ECC', '-DCONFIG_SHA256'} <= set(cpp),
        'personal profile lost an SAE feature')
require(not {'-DIEEE8021X_EAPOL', '-DEAP_TLS'} & set(cpp), 'enterprise EAP enabled')
require('-lcrypto' in libs and '-lssl' not in libs and '-ldes' not in libs,
        'unexpected personal profile libraries')
for conflict in ('EMBER_WPA_PROFILE=invalid', 'EMBER_WPA_PROFILE=personal NO_OPENSSL=1',
                 'EMBER_WPA_PROFILE=personal NO_ENABLE_WPA_SUPPLICANT_EAPOL=1'):
    result = query(*conflict.split())
    require(result.returncode != 0, 'accepted unsupported profile: ' + conflict)
print('PASS: default and personal WPA profile selection')
