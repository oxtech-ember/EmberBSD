#!/usr/bin/env python3
"""Exercise real task lifecycle functions with an early-running child."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
source = (Path(__file__).resolve().parents[1] / 'espressif/esp_wifi_os.c').read_text()

def function(name):
    match = re.search(r'\n(?:static )?(?:void|int|struct espwifi_task \*)\n' +
                      name + r'\(.*?\)\n\{', source, re.S)
    assert match, name
    start = match.start() + 1
    i, depth = match.end() - 1, 0
    while i < len(source):
        if source[i] == '{': depth += 1
        if source[i] == '}':
            depth -= 1
            if depth == 0: return source[start:i + 1]
        i += 1
    raise AssertionError(name)

code = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned int u_int;
typedef int lwp_t;
typedef jmp_buf label_t;
struct espwifi_sem { int dummy; };
struct espwifi_task { lwp_t *t_lwp; void (*t_func)(void *); void *t_arg;
 void *t_stack; struct espwifi_sem *t_sem; int t_fpu_cookie; label_t t_return; };
#define ESPWIFI_STACK_SIZE 16384
#define ESPWIFI_NTHREADS 3
#define M_DEVBUF 0
#define M_NOWAIT 0
#define M_ZERO 1
#define PRI_SOFTNET 0
#define KTHREAD_MPSAFE 0
#define IPL_NONE 0
static int espwifi_mtx, held, fail_alloc, allocations, fail_thread, entries, exits, intr, espwifi_crit_depth;
static struct { int ci_cpl; } cpu;
#define curcpu() (&cpu)
static lwp_t lwp, *curlwp=&lwp;
static struct espwifi_task *espwifi_tasks[ESPWIFI_NTHREADS];
static jmp_buf on_exit;
static void mutex_spin_enter(int *m) { (void)m; assert(!held); held=1; }
static void mutex_spin_exit(int *m) { (void)m; assert(held); held=0; }
static void *alloc(size_t n,int tag,int flags) { (void)tag;(void)flags;
 if (fail_alloc && --fail_alloc==0) return NULL; allocations++; return calloc(1,n); }
static void dealloc(void *p,int tag) { (void)tag; if (p) { allocations--; free(p); } }
static struct espwifi_sem *espwifi_os_sem_create(unsigned a,unsigned b) {
 (void)a;(void)b;return alloc(sizeof(struct espwifi_sem),0,0); }
static void espwifi_os_sem_delete(void *p) { dealloc(p,0); }
#define malloc(n,t,f) alloc(n,t,f)
#define free(p,t) dealloc(p,t)
static int kthread_fpu_enter(void) { entries++; return 7; }
static int cpu_intr_p(void) { return intr==1; }
static int cpu_softintr_p(void) { return intr==2; }
static void kthread_fpu_exit(int cookie) { assert(!held && cookie==7); exits++; }
static void kthread_exit(int x) { assert(x==0); _longjmp(on_exit,1); }
#define setjmp(p) _setjmp(*(p))
#define longjmp(p) _longjmp(*(p),1)
void espwifi_os_task_delete(void *);
static struct espwifi_task *espwifi_task_self(void);
static void espwifi_call_on_stack(void (*f)(void *), void *p, void *stack) {
 assert(stack); f(p); }
static int kthread_create(int pri,int flags,void *cpu,void (*f)(void *),void *arg,
 lwp_t **out,const char *fmt,const char *name) {
 (void)pri;(void)flags;(void)cpu;(void)fmt;(void)name;
 if (fail_thread) return -1;
 /* The child executes before kthread_create publishes *out. */
 if (_setjmp(on_exit)==0) f(arg);
 *out=curlwp; return 0;
}
'''
for name in ('espwifi_task_main', 'espwifi_os_task_create',
             'espwifi_task_self', 'espwifi_os_task_delete'):
    code += '\n' + function(name) + '\n'
code += r'''
static void returned(void *p) { (void)p; assert(espwifi_task_self()!=NULL);
 intr=1; espwifi_os_task_delete(NULL); intr=2; espwifi_os_task_delete(NULL);
 intr=0; cpu.ci_cpl=1; espwifi_os_task_delete(NULL); cpu.ci_cpl=0;
 espwifi_crit_depth=1; espwifi_os_task_delete(NULL); espwifi_crit_depth=0;
 assert(espwifi_task_self()!=NULL && exits==0); }
static void deleted(void *p) { (void)p; assert(espwifi_task_self()!=NULL);
 espwifi_os_task_delete(NULL); assert(0); }
int main(void) {
 lwp_t *handle=NULL;
 espwifi_os_task_delete(NULL); assert(entries==0 && exits==0);
 assert(!espwifi_os_task_create(returned,"large",ESPWIFI_STACK_SIZE+1,0,0));
 for (int n=1;n<=3;n++) { fail_alloc=n;
  assert(!espwifi_os_task_create(returned,"alloc",0,0,0)); assert(!allocations); }
 fail_alloc=0; fail_thread=1;
 assert(!espwifi_os_task_create(returned,"fail",0,0,0)); assert(!allocations);
 fail_thread=0;
 assert(espwifi_os_task_create(returned,"return",0,0,(void **)&handle));
 assert(handle==curlwp && entries==1 && exits==1 && !espwifi_tasks[0] && !allocations);
 assert(espwifi_os_task_create(deleted,"delete",0,0,(void **)&handle));
 assert(entries==2 && exits==2 && !espwifi_tasks[0] && !allocations);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / 'tasks.c'
    out = Path(td) / 'tasks'
    c.write_text(code)
    subprocess.run([a.cc, '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', str(c), '-o', str(out)], check=True)
    subprocess.run([str(out)], check=True)
print('early child identity, FPU lifecycle and creation failures passed')
