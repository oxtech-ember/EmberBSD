#!/usr/bin/env python3
"""Ensure radio workers start only after interface attach succeeds."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
source=(Path(__file__).resolve().parents[1]/'espressif/esp_wifi_os.c').read_text()
match=re.search(r'\nvoid\nespwifi_attach\(void\)\n\{.*?\n\}',source,re.S)
assert match
code=r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define MUTEX_DEFAULT 0
#define IPL_HIGH 1
#define VM_NOSLEEP 0
#define ESPWIFI_POOL_BASE 0x2f030000
#define ESPWIFI_POOL_SIZE 0x40000
typedef unsigned long u_long;
static int espwifi_mtx,espwifi_timer_cv;
static void *espwifi_pool;
static u_long espwifi_pool_free;
static int attach_ok,task_creates,logs;
static void mutex_init(void *m,int t,int i) { (void)m;(void)t;(void)i; }
static void cv_init(void *cv,const char *name) { (void)cv;(void)name; }
static void *vmem_create(const char *name,uintptr_t base,size_t size,size_t align,
 void *a,void *b,void *c,int q,int flags,int ipl) {
 (void)name;(void)base;(void)size;(void)align;(void)a;(void)b;(void)c;
 (void)q;(void)flags;(void)ipl;return (void *)1;
}
static int espwifi_if_attach(void) { return attach_ok; }
static void espwifi_timer_thread(void *v) { (void)v; }
static void espwifi_main(void *v) { (void)v; }
static int espwifi_os_task_create(void (*fn)(void *),const char *name,
 unsigned pri,void *arg,void **handle) {
 (void)fn;(void)name;(void)pri;(void)arg;*handle=(void *)1;task_creates++;return 1;
}
static int printf(const char *fmt,...) { (void)fmt;logs++;return 0; }
''' + match.group() + r'''
int main(void) {
 espwifi_attach();
 assert(task_creates==0 && logs==1);
 attach_ok=1;logs=0;
 espwifi_attach();
 assert(task_creates==2 && logs==0 && espwifi_pool==(void *)1 &&
  espwifi_pool_free==ESPWIFI_POOL_SIZE);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'attach.c';out=Path(td)/'attach';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all',str(c),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('actual ESP attach path gates radio workers on interface initialization')
