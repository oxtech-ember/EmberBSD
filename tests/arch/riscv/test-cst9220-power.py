#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Developed with AI assistance.
"""Check the production deferred touch initialization and supply lifecycle."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def function(source, name):
    pos=source.index('\n'+name+'('); start=source.rfind('\nstatic ',0,pos); opening=source.index('{',pos); depth=0
    for i in range(opening,len(source)):
        if source[i]=='{': depth+=1
        elif source[i]=='}':
            depth-=1
            if depth==0: return source[start+1:i+1]
    raise ValueError(name)


def main():
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--clang',type=Path,required=True);a=p.parse_args()
    src=(a.source/'sys/dev/i2c/cst9220.c').read_text(); func=function(src,'cst_init') + '\n' + function(src,'cst_detach')
    harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct cst9220_softc *device_t;
struct fdtbus_regulator { int unused; };
struct cst9220_softc { void *dev, *mouse, *thread; int phandle; struct fdtbus_regulator *supply; bool ready,dying; uint16_t width,height; };
struct wsmousedev_attach_args { const void *accessops; void *accesscookie; };
static struct fdtbus_regulator rail; static int has_supply,enabled,identify_count,release_count,fail_enable,fail_id;
#define PRI_NONE 0
#define KTHREAD_MUSTJOIN 0
#define CFARGS_NONE 0
#define device_private(x) (x)
#define device_xname(x) "touch"
#define aprint_error_dev(...) ((void)0)
#define aprint_normal_dev(...) ((void)0)
#define aprint_normal(...) ((void)0)
static int of_hasprop(int p,const char *name) { (void)p;(void)name;return has_supply; }
static struct fdtbus_regulator *fdtbus_regulator_acquire(int p,const char *name) { (void)p;(void)name;return &rail; }
static int fdtbus_regulator_enable(struct fdtbus_regulator *p) { (void)p;if (fail_enable)return 1;enabled=1;return 0; }
static void fdtbus_regulator_release(struct fdtbus_regulator *p) { (void)p;release_count++; }
static int cst_identify(struct cst9220_softc *s,uint16_t *p) { (void)s;assert(!has_supply || enabled);identify_count++;*p=0x6854;return fail_id; }
static void cst_poll(void *p) { (void)p; }
static int kthread_create(int a,int b,void *c,void (*f)(void *),void *d,void **e,const char *fmt,const char *name) { (void)a;(void)b;(void)c;(void)f;(void)d;(void)fmt;(void)name;*e=(void *)1;return 0; }
static int kthread_join(void *p) { (void)p;return 0; }
static int config_detach_children(device_t dev, int flags) { (void)dev; (void)flags; return 0; }
static const int cst_accessops=0; static const int wsmousedevprint=0;
static void *config_found(device_t d,void *m,int p,int f) { (void)d;(void)m;(void)p;(void)f;return (void *)1; }
'''
    tests=r'''
int main(void) {
 struct cst9220_softc a={0}; has_supply=1; cst_init(&a);
 assert(enabled && identify_count==1 && a.ready && a.mouse && a.supply==&rail);
 struct cst9220_softc b={0}; enabled=0;fail_enable=1;cst_init(&b);
 assert(!enabled && identify_count==1 && release_count==1 && !b.supply);
 struct cst9220_softc c={0};fail_enable=0;fail_id=1;cst_init(&c);
 assert(enabled && identify_count==2 && release_count==2 && !c.supply);
 assert(cst_detach(&a, 0)==0 && enabled && release_count==3 && !a.supply);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        c=Path(tmp)/'test.c';o=Path(tmp)/'test';c.write_text(harness+'\n'+func+'\n'+tests)
        subprocess.run([str(a.clang),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(o)],check=True);subprocess.run([str(o)],check=True)
    print('CST9220 supply lifecycle tests passed')
if __name__=='__main__':main()
