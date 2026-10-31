#!/usr/bin/env python3
"""Exercise the actual kernel/radio constant-time comparison bridge."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', required=True)
a = p.parse_args()
sysdir = Path(__file__).resolve().parents[3]
root = sysdir.parent
files = [sysdir/'arch/riscv/espressif/wifi/glue.c',
         sysdir/'arch/riscv/espressif/esp_wifi_os.c',
         root/'common/lib/libc/string/consttime_memequal.c']
names = ['os_memcmp_const', 'espwifi_os_ct_equal', 'consttime_memequal']
functions = []
for path, name in zip(files, names):
    match = re.search(r'\n(?:int\s*\n|int\s+)'+name+r'\([^)]*\)\n\{.*?\n\}', path.read_text(), re.S)
    if not match:
        raise SystemExit('missing actual function: '+name)
    functions.append(match.group())
source = '''#include <stddef.h>\n#include <assert.h>\n#include <string.h>\nint consttime_memequal(const void *, const void *, size_t);\nint espwifi_os_ct_equal(const void *, const void *, unsigned long);\n'''
source += '\n'.join(reversed(functions))
source += '''
int main(void) {
 unsigned char a[8] = {0,1,2,3,4,5,6,7}, b[8];
 memcpy(b,a,sizeof(a));
 assert(os_memcmp_const(a,b,sizeof(a)) == 0);
 assert(os_memcmp_const(a,b,0) == 0);
 for (unsigned int i=0; i<sizeof(a); i++) {
  b[i] ^= 1;
  assert(os_memcmp_const(a,b,sizeof(a)) != 0);
  b[i] ^= 1;
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'test.c'; exe=Path(td)/'test'; c.write_text(source)
    subprocess.run([a.cc,'-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('actual kernel/radio comparison bridge passed')
