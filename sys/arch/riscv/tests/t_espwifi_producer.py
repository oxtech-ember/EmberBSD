#!/usr/bin/env python3
"""The radio producer requires explicit paths and fails before making output."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys
import struct
import tempfile

if not __debug__:
    sys.exit('this test requires Python assertions enabled')

p=argparse.ArgumentParser();p.add_argument('--python',required=True)
p.add_argument('--host-prefix',required=True);p.add_argument('--sysroot',required=True)
p.add_argument('--target-binutils-prefix',required=True);a=p.parse_args()
script=Path(__file__).resolve().parents[1]/'espressif/wifi/mkblob.py'
with tempfile.TemporaryDirectory() as td:
 output=Path(td)/'radio.o'
 r=subprocess.run([a.python,'-B',str(script),'--external-wpa2',
                   '--output',str(output)],text=True,capture_output=True)
 assert r.returncode != 0
 assert 'required' in r.stderr
 assert not output.exists()
 output.write_bytes(b'previous artifact')
 r=subprocess.run([a.python,'-B',str(script),'--external-wpa2',
                   '--idf-source',td,'--wpa-source',td,
                   '--sysroot',a.sysroot,'--host-prefix',a.host_prefix,
                   '--target-binutils-prefix',a.target_binutils_prefix,
                   '--output',str(output)],text=True,capture_output=True)
 assert r.returncode != 0
 assert output.read_bytes()==b'previous artifact'
print('explicit input contract passed')
spec=importlib.util.spec_from_file_location('radio_producer',script)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.check_consumer_boundary(m.ENTRIES, script.parent.parent/'esp_wifi_os.h',
    [script.parent.parent/'esp_wifi_if.c', script.parent.parent/'esp_wifi_os.c'])
try:
    m.check_consumer_boundary([e for e in m.ENTRIES if e != 'espwifi_disconnect'],
        script.parent.parent/'esp_wifi_os.h',
        [script.parent.parent/'esp_wifi_if.c', script.parent.parent/'esp_wifi_os.c'])
except ValueError as e:
    assert 'espwifi_disconnect' in str(e)
else:
    raise AssertionError('kernel disconnect caller was not required by producer')
print('kernel consumer boundary guard passed')
m.check_mac_leaf('  0: 000005b7 lui a1, 0\n  4: 8082 ret\n')
for text in ('  0: 000005b7 lui a1, 0\n  4: 00f call 0\n  8: 8082 ret\n',
             '  0: 12345678 fadd.s fa0, fa0, fa1\n  4: 8082 ret\n', ''):
    try:
        m.check_mac_leaf(text)
    except ValueError:
        pass
    else:
        raise AssertionError('unsafe or missing get_mac accepted')
print('integer-only MAC entry guard passed')
header=bytearray(52)
header[:7]=b'\x7fELF\x01\x01\x01'
struct.pack_into('<H',header,16,1)
struct.pack_into('<H',header,18,243)
struct.pack_into('<I',header,36,3)
m.check_vendor_elf(header)
for offset, value in ((16,2),(18,62),(36,1),(36,5),(36,10)):
    bad=bytearray(header)
    struct.pack_into('<I' if offset==36 else '<H',bad,offset,value)
    try:
        m.check_vendor_elf(bad)
    except ValueError:
        pass
    else:
        raise AssertionError('unexpected target or ABI accepted')
print('RISC-V vendor object ABI guard passed')
m.check_f_only('  10: fadd.s fa0, fa1, fa2\n  14: fcvt.s.w fa0, a0\n')
for op in ('fld', 'fsd', 'c.fld', 'c.fsdsp', 'fadd.d', 'fcvt.s.d', 'fcvt.d.w'):
    try:
        m.check_f_only('  10: '+op+' fa0, fa1\n')
    except ValueError:
        pass
    else:
        raise AssertionError('double instruction accepted: '+op)
print('F-only instruction guard passed')
