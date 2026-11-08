#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Execute production WPA/RSN cipher negotiation parsers with bounded IEs."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


def function(source, name):
    found = re.search(r'^static int\n' + name + r'\([\s\S]+?^}', source, re.M)
    if found is None:
        raise ValueError(name)
    return found.group()


def macro(source, name):
    lines = source.splitlines()
    for i, line in enumerate(lines):
        if re.match(r'^#define\s+' + name + r'\b', line):
            result = [line]
            while result[-1].endswith('\\'):
                i += 1
                result.append(lines[i])
            return '\n'.join(result)
    raise ValueError(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', required=True)
    parser.add_argument('--cflags', default='')
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    net = root / 'sys/net80211'
    source = (net/'ieee80211_input.c').read_text()
    wire = (net/'ieee80211.h').read_text()
    crypto = (net/'ieee80211_crypto.h').read_text()
    constants = []
    for content, pattern in ((wire, r'(?:WPA|RSN)_(?:OUI|VERSION|CSE_\w+|ASE_\w+)'),
                             (crypto, r'IEEE80211_CIPHER_\w+')):
        for name in re.findall(r'^#define\s+(' + pattern + r')\b', content, re.M):
            constants.append(macro(content, name))
    variables = (net/'ieee80211_var.h').read_text()
    constants += [macro(variables, name) for name in ('IEEE80211_F_WPA1', 'IEEE80211_F_WPA2')]
    constants += [macro(source, name) for name in ('LE_READ_2', 'LE_READ_4')]
    rsn = re.search(r'^struct ieee80211_rsnparms \{[\s\S]+?^};',
                    (net/'ieee80211_node.h').read_text(), re.M).group()
    implementation = '\n'.join(function(source, name) for name in (
        'wpa_cipher', 'wpa_keymgmt', 'ieee80211_parse_wpa',
        'rsn_cipher', 'rsn_keymgmt', 'ieee80211_parse_rsn'))
    prelude = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
#define NBBY 8
#define IEEE80211_REASON_IE_INVALID 13
#define IEEE80211_MSG_ELEMID 1
#define IEEE80211_MSG_WPA 2
struct ieee80211com { unsigned ic_flags; };
struct ieee80211_frame { uint8_t bytes[24]; };
static void discard(const void *ic, unsigned msg, const void *wh,
    const char *protocol, const char *format, ...) {
    (void)ic; (void)msg; (void)wh; (void)protocol; (void)format;
}
#define IEEE80211_DISCARD_IE(...) discard(__VA_ARGS__)
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#c); exit(1); } } while(0)
'''
    tests = r'''
static void selector(uint8_t *out, uint32_t oui, unsigned type) {
    out[0]=oui; out[1]=oui>>8; out[2]=oui>>16; out[3]=type;
}
static unsigned element(uint8_t ie[64], bool wpa, const unsigned *types, unsigned count) {
    unsigned pos=2; uint32_t oui=wpa?WPA_OUI:RSN_OUI;
    memset(ie,0,64); ie[0]=wpa?221:48;
    if(wpa) { selector(ie+pos,WPA_OUI,1); pos+=4; }
    ie[pos++]=1; ie[pos++]=0;
    selector(ie+pos,oui,4); pos+=4;
    ie[pos++]=count; ie[pos++]=0;
    for(unsigned i=0;i<count;i++) { selector(ie+pos,oui,types[i]); pos+=4; }
    ie[pos++]=1; ie[pos++]=0; selector(ie+pos,oui,2); pos+=4;
    ie[1]=pos-2; return pos;
}
static int parse(bool wpa, uint8_t *ie, uint8_t configured, struct ieee80211_rsnparms *rsn) {
    struct ieee80211com ic={.ic_flags=IEEE80211_F_WPA1|IEEE80211_F_WPA2};
    struct ieee80211_frame wh={0};
    memset(rsn,0,sizeof(*rsn)); rsn->rsn_mcastcipher=IEEE80211_CIPHER_AES_CCM;
    rsn->rsn_ucastcipherset=configured; rsn->rsn_ucastcipher=0xaa;
    rsn->rsn_ucastkeylen=16; rsn->rsn_keymgmtset=2;
    return wpa?ieee80211_parse_wpa(&ic,ie,rsn,&wh):ieee80211_parse_rsn(&ic,ie,rsn,&wh);
}
int main(void) {
    uint8_t ie[64]; struct ieee80211_rsnparms rsn;
    const uint8_t allowed=(1U<<IEEE80211_CIPHER_TKIP)|(1U<<IEEE80211_CIPHER_AES_CCM);
    for(unsigned protocol=0;protocol<2;protocol++) {
        bool wpa=protocol!=0;
        const unsigned unknown[]={255}; element(ie,wpa,unknown,1);
        CHECK(parse(wpa,ie,allowed,&rsn)==IEEE80211_REASON_IE_INVALID);
        CHECK(rsn.rsn_ucastcipher==0xaa);
        const unsigned rejected[]={0,1,3,5,6,254};
        for(unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);i++) {
            element(ie,wpa,&rejected[i],1);
            CHECK(parse(wpa,ie,255,&rsn)==IEEE80211_REASON_IE_INVALID);
            CHECK(rsn.rsn_ucastcipher==0xaa);
            CHECK(rsn.rsn_ucastkeylen==16);
        }
        const unsigned none[]={0}; element(ie,wpa,none,1);
        CHECK(parse(wpa,ie,1U<<IEEE80211_CIPHER_NONE,&rsn)==IEEE80211_REASON_IE_INVALID);
        CHECK(rsn.rsn_ucastcipher==0xaa);
        const unsigned ccmp[]={4}; element(ie,wpa,ccmp,1);
        CHECK(parse(wpa,ie,allowed,&rsn)==0 && rsn.rsn_ucastcipher==IEEE80211_CIPHER_AES_CCM);
        CHECK(parse(wpa,ie,1U<<IEEE80211_CIPHER_TKIP,&rsn)==IEEE80211_REASON_IE_INVALID);
        const unsigned tkip[]={2}; element(ie,wpa,tkip,1);
        CHECK(parse(wpa,ie,allowed,&rsn)==0 && rsn.rsn_ucastcipher==IEEE80211_CIPHER_TKIP);
        const unsigned both[]={4,2}; element(ie,wpa,both,2);
        CHECK(parse(wpa,ie,allowed,&rsn)==0 && rsn.rsn_ucastcipher==IEEE80211_CIPHER_TKIP);
        CHECK(parse(wpa,ie,1U<<IEEE80211_CIPHER_AES_CCM,&rsn)==0 && rsn.rsn_ucastcipher==IEEE80211_CIPHER_AES_CCM);
        const unsigned mixed[]={255,5,4}; element(ie,wpa,mixed,3);
        CHECK(parse(wpa,ie,allowed,&rsn)==0 && rsn.rsn_ucastcipher==IEEE80211_CIPHER_AES_CCM);
        CHECK(rsn.rsn_ucastkeylen==16);
        element(ie,wpa,ccmp,1); unsigned pair=wpa?14:10;
        ie[pair]^=0x80;
        CHECK(parse(wpa,ie,255,&rsn)==IEEE80211_REASON_IE_INVALID);
        element(ie,wpa,ccmp,1); ie[wpa?8:4]^=0x80;
        CHECK(parse(wpa,ie,allowed,&rsn)==IEEE80211_REASON_IE_INVALID);
        element(ie,wpa,ccmp,0);
        CHECK(parse(wpa,ie,allowed,&rsn)==IEEE80211_REASON_IE_INVALID);
        unsigned size=element(ie,wpa,ccmp,1);
        for(unsigned length=0;length<size-2;length++) {
            ie[1]=length;
            CHECK(parse(wpa,ie,allowed,&rsn)==IEEE80211_REASON_IE_INVALID);
        }
        element(ie,wpa,ccmp,1); ie[pair-2]=255; ie[pair-1]=255;
        CHECK(parse(wpa,ie,allowed,&rsn)==IEEE80211_REASON_IE_INVALID);
    }
    printf("WPA/RSN pairwise parser: %u checks passed\n",checks); return 0;
}
'''
    args.build_dir.mkdir(parents=True, exist_ok=True)
    source_file = args.build_dir/'cipher-parser-contract.c'
    source_file.write_text(prelude+'\n'+'\n'.join(constants)+'\n'+rsn+'\n'+implementation+tests)
    binary = args.build_dir/'cipher-parser-contract'
    cmd = shlex.split(args.cc)+['-std=c11','-Wall','-Wextra','-Werror']
    cmd += shlex.split(args.cflags)+[str(source_file),'-o',str(binary)]
    (args.build_dir/'compile_commands.json').write_text(json.dumps([{
        'directory':str(root),'file':str(source_file.resolve()),'arguments':cmd}],indent=2)+'\n')
    subprocess.run(cmd,check=True)
    subprocess.run(shlex.split(os.environ.get('TEST_RUNNER',''))+[str(binary.resolve())],check=True)


if __name__ == '__main__':
    main()
