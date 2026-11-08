#!/usr/bin/env python3
"""Exercise opt-in accounting for ESP32-S31 internal-pool allocation."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
p.add_argument('--analyze', action='store_true')
p.add_argument('--cppcheck')
a = p.parse_args()

arch = Path(__file__).resolve().parents[1]
os_source = (arch / 'espressif/esp_wifi_os.c').read_text()
os_header = (arch / 'espressif/esp_wifi_os.h').read_text()


def function(name, result):
    match = re.search(r'\n(?:static )?' + re.escape(result) + r'\n' +
                      name + r'\(.*?\n\}', os_source, re.S)
    assert match, name
    return match.group()


diag_type = re.search(r'^struct espwifi_alloc_diag \{.*?^\};$',
                      os_header, re.M | re.S)
assert diag_type, 'allocator diagnostic snapshot type missing'
diag_state = re.search(
    r'^static volatile bool espwifi_alloc_diag_enabled;\n'
    r'^static bool espwifi_alloc_diag_initialized;\n'
    r'^static unsigned int espwifi_alloc_diag_window_state;\n'
    r'^static u_long espwifi_alloc_diag_pool_min_free;\n'
    r'^static u_long espwifi_alloc_diag_window_min_free;\n'
    r'^static u_long espwifi_alloc_diag_pool_misses;\n'
    r'^static u_long espwifi_alloc_diag_fallback_successes;\n'
    r'^static u_long espwifi_alloc_diag_fallback_requested_bytes;\n'
    r'^static u_long espwifi_alloc_diag_final_nulls;$', os_source, re.M)
assert diag_state, 'allocator diagnostic state missing'
header_block = diag_type.group() + '\n' + diag_state.group()
allocator = function('espwifi_alloc', 'void *')
enable = function('espwifi_os_alloc_diag_enable', 'void')
window_begin = function('espwifi_os_alloc_diag_window_begin', 'void')
window_end = function('espwifi_os_alloc_diag_window_end', 'void')
snapshot = function('espwifi_os_alloc_diag_snapshot', 'void')

hdr = re.search(r'^struct espwifi_hdr \{.*?^\};$', os_source, re.M | re.S)
assert hdr, 'allocator header missing'

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef unsigned long u_long;
typedef uintptr_t vmem_addr_t;
typedef struct test_vmem vmem_t;
struct test_vmem { unsigned unused; };
#define ESPWIFI_POOL_SIZE 4096
#define VM_INSTANTFIT 1
#define VM_NOSLEEP 2
#define M_DEVBUF 1
#define M_NOWAIT 2
#define roundup2(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
static vmem_t pool_object;
static vmem_t *espwifi_pool = &pool_object;
static u_long espwifi_pool_free = ESPWIFI_POOL_SIZE;
static unsigned critical_depth;
static unsigned char pool_memory[ESPWIFI_POOL_SIZE];
static unsigned char fallback_memory[ESPWIFI_POOL_SIZE];
static size_t pool_used, fallback_used;
static bool vmem_fail, malloc_fail;
static unsigned pool_frees, fallback_frees;
static unsigned int espwifi_os_critical_enter(void) {
 critical_depth++;
 return critical_depth;
}
static void espwifi_os_critical_exit(unsigned int token) {
 assert(token == critical_depth);
 critical_depth--;
}
static void atomic_add_long(u_long *p, long delta) {
 *p = (u_long)((long)*p + delta);
}
static u_long atomic_add_long_nv(u_long *p, long delta) {
 *p = (u_long)((long)*p + delta);
 return *p;
}
static int vmem_alloc(vmem_t *v, size_t size, int flags, vmem_addr_t *addr) {
 assert(v == espwifi_pool && flags == (VM_INSTANTFIT | VM_NOSLEEP));
 assert(critical_depth == 0);
 if (vmem_fail || size > sizeof(pool_memory) - pool_used)
  return 1;
 *addr = (vmem_addr_t)(pool_memory + pool_used);
 pool_used += size;
 return 0;
}
static void vmem_free(vmem_t *v, vmem_addr_t addr, size_t size) {
 assert(v == espwifi_pool && addr != 0 && size != 0);
 pool_frees++;
 pool_used = 0;
}
static void *test_malloc(size_t size, int type, int flags) {
 unsigned char *p;
 assert(type == M_DEVBUF && flags == M_NOWAIT && critical_depth == 0);
 if (malloc_fail || size > sizeof(fallback_memory) - fallback_used)
  return NULL;
 p = fallback_memory + fallback_used;
 fallback_used += size;
 return p;
}
static void test_free(void *p, int type) {
 assert(p != NULL && type == M_DEVBUF);
 fallback_frees++;
 fallback_used = 0;
}
#define malloc test_malloc
#define free test_free
''' + diag_type.group() + '\n' + hdr.group() + '\n' + \
    diag_state.group() + '\n' + allocator + '\n' + enable + '\n' + \
    window_begin + '\n' + window_end + '\n' + snapshot + r'''
int main(void) {
 struct espwifi_alloc_diag stats, init, begin, end, after;
 struct espwifi_hdr *header;
 void *p;
 size_t first_total = roundup2(64 + sizeof(struct espwifi_hdr), 16);

 assert(critical_depth == 0 && !espwifi_alloc_diag_enabled);
 vmem_fail = true;
 p = espwifi_alloc(32, true);
 assert(p != NULL);
 espwifi_os_alloc_diag_snapshot(&stats);
 assert(stats.pool_misses == 0 && stats.fallback_successes == 0 &&
     stats.final_nulls == 0);
 header = (struct espwifi_hdr *)p - 1;
 test_free(header, M_DEVBUF);

 espwifi_os_alloc_diag_enable();
 espwifi_os_alloc_diag_snapshot(&stats);
 assert(stats.pool_free == ESPWIFI_POOL_SIZE &&
     stats.pool_min_free == ESPWIFI_POOL_SIZE);
 vmem_fail = false;
 p = espwifi_alloc(64, true);
 assert(p != NULL && espwifi_pool_free == ESPWIFI_POOL_SIZE - first_total);
 espwifi_os_alloc_diag_snapshot(&stats);
 assert(stats.pool_min_free == ESPWIFI_POOL_SIZE - first_total);
 header = (struct espwifi_hdr *)p - 1;
 vmem_free(espwifi_pool, (vmem_addr_t)header, header->h_size);
 atomic_add_long(&espwifi_pool_free, header->h_size);
 espwifi_os_alloc_diag_snapshot(&init);
 assert(init.pool_free == ESPWIFI_POOL_SIZE &&
     init.pool_min_free == ESPWIFI_POOL_SIZE - first_total &&
     init.pool_misses == 0 && init.fallback_successes == 0 &&
     init.final_nulls == 0);
 malloc_fail = true;
 p = espwifi_alloc(11, false);
 assert(p == NULL);
 espwifi_os_alloc_diag_snapshot(&stats);
 assert(stats.pool_misses == 0 && stats.fallback_successes == 0 &&
     stats.final_nulls == 0);
 malloc_fail = false;

 espwifi_os_alloc_diag_window_begin();
 espwifi_os_alloc_diag_snapshot(&begin);
 assert(begin.pool_free == ESPWIFI_POOL_SIZE &&
     begin.window_min_free == ESPWIFI_POOL_SIZE);
 p = espwifi_alloc(64, true);
 assert(p != NULL);
 header = (struct espwifi_hdr *)p - 1;
 vmem_free(espwifi_pool, (vmem_addr_t)header, header->h_size);
 atomic_add_long(&espwifi_pool_free, header->h_size);
 espwifi_os_alloc_diag_window_begin();
 vmem_fail = true;
 p = espwifi_alloc(37, true);
 assert(p != NULL);
 header = (struct espwifi_hdr *)p - 1;
 test_free(header, M_DEVBUF);
 malloc_fail = true;
 p = espwifi_alloc(19, true);
 assert(p == NULL);
 espwifi_os_alloc_diag_window_end();
 espwifi_os_alloc_diag_snapshot(&end);
 assert(end.pool_free == ESPWIFI_POOL_SIZE &&
     end.window_min_free == ESPWIFI_POOL_SIZE - first_total &&
     end.pool_misses == 2 && end.fallback_successes == 1 &&
     end.fallback_requested_bytes == 37 && end.final_nulls == 1);

 espwifi_os_alloc_diag_enable();
 p = espwifi_alloc(10, true);
 assert(p == NULL);
 espwifi_os_alloc_diag_snapshot(&after);
 assert(after.pool_misses == end.pool_misses &&
     after.fallback_successes == end.fallback_successes &&
     after.fallback_requested_bytes == end.fallback_requested_bytes &&
     after.final_nulls == end.final_nulls);
 assert(critical_depth == 0 && pool_frees == 2 && fallback_frees == 2);
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='espwifi-alloc-test-') as td:
    td = Path(td)
    cfile = td / 'alloc.c'
    binary = td / 'alloc'
    cfile.write_text(code)
    if a.cppcheck:
        subprocess.run([a.cppcheck,
                        '--enable=warning,performance,portability',
                        '--error-exitcode=1', '--language=c', '--std=c11',
                        str(cfile)], check=True)
        raise SystemExit(0)
    flags = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
             str(cfile), '-o', str(binary)]
    if a.analyze:
        subprocess.run([a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '--analyze', str(cfile), '-o', str(td / 'alloc.plist')],
                       check=True)
    subprocess.run(flags, check=True)
    subprocess.run([str(binary)], check=True)
