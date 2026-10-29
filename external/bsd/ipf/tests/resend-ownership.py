#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Exercise ordinary resend ownership paths without network or device IO."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
for name in ('compiler', 'linker', 'host-sdk'):
    p.add_argument('--' + name, required=True)
a = p.parse_args()
source = Path(__file__).resolve().parents[1] / 'dist/ipsend/resend.c'
text = source.read_text()
start = text.index('int\tip_resend(')
function = text[start:]
assert function.count('int\tip_resend(') == 1
harness = r'''
#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
typedef struct { unsigned char ether_dhost[6], ether_shost[6]; uint16_t ether_type; } ether_header_t;
struct ip { unsigned short ip_len, ip_sum; };
typedef struct ip ip_t;
typedef struct { char mb_buf[256]; } mb_t;
struct ipread { int (*r_open)(char *); int (*r_readip)(mb_t *, void *, void *); int (*r_close)(void); };
#define OPT_RAW 1
#define ETHERTYPE_IP 0x800
#define A_A
#define IP_HL(ip) 5
int opts;
static int seen;
static int initdevice(char *dev, int timeout) { (void)dev; (void)timeout; return 3; }
static int arp(char *addr, char *dst) { (void)addr; bzero(dst, 6); return 0; }
static unsigned short chksum(unsigned short *ip, int size) { (void)ip; (void)size; return 0; }
static void dumppacket(ip_t *ip) { (void)ip; }
static int sendip(int fd, char *buf, int len) { (void)fd; (void)buf; return len > 0 ? 0 : -1; }
static int ropen(char *path) { (void)path; seen = 0; return 4; }
static int rread(mb_t *m, void *a, void *b) {
    (void)a; (void)b;
    if (seen++) return 0;
    bzero(m->mb_buf, sizeof(m->mb_buf));
    ((ip_t *)m->mb_buf)->ip_len = htons(sizeof(ip_t));
    return sizeof(ip_t);
}
static int rclose(void) { return 0; }
'''
main = r'''
int main(void) {
    struct ipread reader = { ropen, rread, rclose };
    struct in_addr gw; gw.s_addr = htonl(0x7f000001);
    opts = 0; if (ip_resend("mock", 1500, &reader, gw, "fixture")) return 1;
    opts = OPT_RAW; if (ip_resend("mock", 1500, &reader, gw, "fixture")) return 1;
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'test.c'
    exe = Path(tmp) / 'test'
    c.write_text(harness + function + main)
    subprocess.run([a.compiler, '--ld-path=' + a.linker, '--sysroot=' + a.host_sdk,
                    '-std=gnu11', '-fsanitize=address,undefined', '-g', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('ordinary raw and framed resend with mocked transport: PASS (ASan/UBSan)')
