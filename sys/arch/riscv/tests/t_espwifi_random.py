#!/usr/bin/env python3
"""Exercise actual secret random policy with a bounded deterministic mock."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
p=argparse.ArgumentParser();p.add_argument('--cc',required=True);a=p.parse_args()
s=(Path(__file__).resolve().parents[1]/'espressif/esp_wifi_os.c').read_text()
m=re.search(r'\nint\s*\nespwifi_os_secret_random\([^)]*\)\n\{.*?\n\}',s,re.S)
if not m: raise SystemExit('missing actual secret random function')
code='''#include <stddef.h>\n#include <stdint.h>\n#define IPL_NONE 0\n#define MIN(a,b) ((a)<(b)?(a):(b))\n#include <stdbool.h>\n#include <string.h>\n#include <assert.h>\nstatic int intr,critical,ready,calls,failcall;\nstatic struct { int ci_cpl; } cpu;\n#define curcpu() (&cpu)\n#define espwifi_crit_depth critical\n#define cpu_intr_p() (intr)\n#define entropy_ready() (ready)\n#define kern_cprng ((void *)0)\n#define explicit_memset(p,v,n) memset(p,v,n)\nstatic size_t cprng_strong(void *p, void *v, size_t n, int f){(void)p;(void)f;calls++;memset(v,0xa5,n);return calls==failcall?n-1:n;}\n'''+m.group()+'''\nint main(void){ unsigned char b[100]; ready=1;assert(espwifi_os_secret_random(b,sizeof(b))==0);assert(calls==4);calls=0;failcall=2;assert(espwifi_os_secret_random(b,sizeof(b))!=0);for(unsigned i=0;i<sizeof(b);i++)assert(b[i]==0);failcall=0;calls=4;ready=1;assert(espwifi_os_secret_random(b,sizeof(b))==0);for(unsigned i=0;i<sizeof(b);i++)assert(b[i]==0xa5); calls=0; ready=0;assert(espwifi_os_secret_random(b,0)==0);assert(calls==0);ready=1; intr=1;assert(espwifi_os_secret_random(b,sizeof(b))!=0);assert(calls==0);intr=0;cpu.ci_cpl=1;assert(espwifi_os_secret_random(b,sizeof(b))!=0);assert(calls==0);cpu.ci_cpl=0;critical=1;assert(espwifi_os_secret_random(b,sizeof(b))!=0);assert(calls==0);critical=0;ready=0;assert(espwifi_os_secret_random(b,sizeof(b))!=0);assert(calls==0);return 0;}\n'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'t.c';exe=Path(td)/'t';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('strong RNG readiness and context handling passed')
glue=(Path(__file__).resolve().parents[1]/'espressif/wifi/glue.c').read_text()
wrapped=[]
for name in ('os_get_random','get_random_wrapper'):
 match=re.search(r'\n(?:static\s+)?int\s*\n'+name+r'\([^)]*\)\n\{.*?\n\}',glue,re.S)
 if not match: raise SystemExit('missing random wrapper: '+name)
 wrapped.append(match.group())
code='''#include <stddef.h>\n#include <stdint.h>\n#include <assert.h>\nstatic int espwifi_os_secret_random(void *p, unsigned long n) { (void)p; (void)n; return -1; }\n'''+''.join(wrapped)+'''\nint main(void) { uint8_t b=7; assert(os_get_random(&b,1)==-1); assert(get_random_wrapper(&b,1)==-1); assert(b==7); return 0; }\n'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'t.c';exe=Path(td)/'t';c.write_text(code)
 subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('vendor random callbacks propagate failure')
