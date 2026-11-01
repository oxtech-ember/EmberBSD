#!/usr/bin/env python3
"""Check actual RV32 call sequences across the radio's integer ABI boundary."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--clang', required=True, type=Path)
p.add_argument('--sysroot', required=True, type=Path)
p.add_argument('--object', required=True, type=Path)
p.add_argument('--readelf', required=True, type=Path)
p.add_argument('--nm', required=True, type=Path)
a = p.parse_args()
header = Path(__file__).resolve().parents[1] / 'esp_wifi_os.h'
src = r'''
#include "esp_wifi_os.h"
#include <stdarg.h>
__attribute__((noinline)) int a(void (*f)(void *), const char *n, unsigned int s, void *v, void **h) { return espwifi_os_task_create(f,n,s,v,h); }
__attribute__((noinline)) unsigned int b(void *p, unsigned int x, int y, int z, unsigned int t) { return espwifi_os_event_wait(p,x,y,z,t); }
__attribute__((noinline)) long long c(void) { return espwifi_os_time_us(); }
__attribute__((noinline)) void d(void (*f)(void *), void *p) { espwifi_os_intr_establish(3,f,p); }
__attribute__((noinline)) void e(const char *fmt, ...) { __builtin_va_list ap; __builtin_va_start(ap,fmt); espwifi_os_vlog(fmt,ap); __builtin_va_end(ap); }
__attribute__((noinline)) int f(const char *s, const char *p) { return espwifi_connect(s,p); }
__attribute__((noinline)) void g(unsigned char *p) { espwifi_get_mac(p); }
/* A float argument really is different and must never cross this boundary. */
__attribute__((noinline)) float h(float x) { return x + 1.0f; }
'''
with tempfile.TemporaryDirectory() as td:
    path = Path(td) / 'abi.c'
    path.write_text(src)
    resource = subprocess.check_output([str(a.clang), '-print-resource-dir'], text=True).strip()
    results = []
    for abi in ('ilp32', 'ilp32f'):
        cmd = [str(a.clang), '--target=riscv32--netbsd', '--sysroot='+str(a.sysroot),
               '-nostdinc', '-isystem', str(a.sysroot/'usr/include'), '-isystem', resource+'/include',
               '-I', str(header.parent), '-march=rv32imafc_zicsr_zifencei', '-mabi='+abi,
               '-ffreestanding', '-O2', '-S', str(path), '-o', '-']
        asm = subprocess.check_output(cmd, text=True)
        bodies = []
        for name in 'abcdefgh':
            match = re.search(r'^'+name+r':.*?^\s*\.size\s+'+name+r',', asm, re.M | re.S)
            if not match:
                raise SystemExit('missing function '+name)
            # Directives and comments contain target attributes and source locations;
            # compare the generated instructions and control-flow labels themselves.
            body = [line.strip().split('#',1)[0].strip() for line in match.group().splitlines()[1:]
                    if line.strip() and not line.lstrip().startswith(('.', '#'))]
            bodies.append(body)
        results.append(bodies)
    if results[0][:7] != results[1][:7]:
        raise SystemExit('soft and single-float call sequences differ: '+repr(results))
    if results[0][7] == results[1][7]:
        raise SystemExit('float ABI negative control did not differ')
    declarations = []
    for abi in ('ilp32', 'ilp32f'):
        cmd = [str(a.clang), '--target=riscv32--netbsd', '--sysroot='+str(a.sysroot),
               '-nostdinc', '-isystem', str(a.sysroot/'usr/include'), '-isystem', resource+'/include',
               '-march=rv32imafc_zicsr_zifencei', '-mabi='+abi, '-include', 'stdio.h',
               '-fsyntax-only', '-Xclang', '-ast-dump=json', str(header)]
        tree = json.loads(subprocess.check_output(cmd))
        found = {}
        for node in tree['inner']:
            name = node.get('name', '')
            if node.get('kind') != 'FunctionDecl' or not (name.startswith('espwifi_') or name == 'vsnprintf'):
                continue
            qual = node['type']['qualType']
            words = set(re.findall(r'[A-Za-z_][A-Za-z_0-9]*', qual))
            allowed = {'void','int','long','unsigned','char','const','restrict',
                       '__size_t','__builtin_va_list','__attribute__','noreturn'}
            if words - allowed:
                raise SystemExit('unverified boundary type '+name+': '+qual)
            found[name] = qual
        declarations.append(found)
    if declarations[0] != declarations[1]:
        raise SystemExit('boundary declarations differ across ABIs')
    undefined = set()
    for line in subprocess.check_output([str(a.nm), '-u', str(a.object)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 2 and fields[0] == 'U':
            undefined.add(fields[1])
    relocations = subprocess.check_output([str(a.readelf), '-r', str(a.object)], text=True)
    live = set()
    for line in relocations.splitlines():
        fields = line.split()
        if len(fields) >= 5 and fields[2].startswith('R_RISCV_') and fields[4] in undefined:
            live.add(fields[4])
    if not live:
        raise SystemExit('radio object has no live external relocations')
    if live - declarations[0].keys():
        raise SystemExit('uncovered live external symbols: '+repr(live - declarations[0].keys()))
    print('Checked', len(declarations[0]), 'declarations and', len(live), 'live external symbols')
print('RV32 ilp32/ilp32f integer, callback, 64-bit and variadic call sequences agree')
