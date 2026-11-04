#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
import argparse, os, pathlib, shutil, subprocess, tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--awk', required=True, help='POSIX awk executable')
parser.add_argument('--shell', required=True, help='shell executable')
parser.add_argument('--tool-path', required=True, help='selected native tool PATH')
parser.add_argument('--windows-source', default=r'C:\Prepared Sources\zlib')
args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[2]/'sys/arch/hpc/stand'
env=dict(os.environ, AWK=args.awk, PATH=args.tool_path, EMBER_ZLIB_SOURCE=args.windows_source)
shell=args.shell
# Preserve the old GNU gensub escaping, including non-overlapping ## matches.
awk_source=(root/'dspgen/do_subst.awk').read_text()
function=awk_source[awk_source.index('function cppdef_spaces'):awk_source.index('\n# Expand a leading')]
cases=['#a','a#b','##','a##b',r'a\#b',r'a\\#b',r'\##','a#b#c']
expected=[' a','a b','# ','a #b','a#b',r'a\#b','# ','a b c']
result=subprocess.run([args.awk, function+'\n{ print cppdef_spaces($0) }'],input='\n'.join(cases)+'\n',text=True,capture_output=True,check=True)
assert result.stdout.splitlines()==expected, result.stdout
with tempfile.TemporaryDirectory() as tmp:
 p=pathlib.Path(tmp)
 shutil.copytree(root/'dspgen',p/'dspgen')
 for name in ('libz','hpcboot'):
  (p/name).mkdir(); shutil.copy(root/name/(name+'.config'),p/name)
 for invalid in ('', '/unix/zlib'):
  bad=subprocess.run([shell,'dspgen/config.sh','vc5','libz'],cwd=p,env=dict(env,EMBER_ZLIB_SOURCE=invalid),capture_output=True,text=True)
  assert bad.returncode != 0 and 'absolute Windows' in bad.stderr
 for version,arch in [('vc5',['SH','MIPS']),('vc6',['ARM','SH3','SH4','MIPS']),('evc3',['ARM','SH3','SH4','MIPS']),('evc4',['ARMV4','SH3','SH4'])]:
  for kind in ('application','static_library'):
   subprocess.run([shell,'gen_template.sh',version,*arch],cwd=p/'dspgen',env=dict(env,TYPE=kind),check=True,stdout=subprocess.DEVNULL)
  for name in ('libz','hpcboot'):
   subprocess.run([shell,'dspgen/config.sh',version,name],cwd=p,env=env,check=True)
   data=(p/name/(name+('.dsp' if version.startswith('vc') else '.vcp'))).read_text()
   assert '/I "'+env['EMBER_ZLIB_SOURCE']+'"' in data,(version,name,'include')
   assert '$(EMBER_ZLIB_SOURCE)' not in data
   assert 'ZLIB_NETBSD_STANDALONE' in data,(version,name,'define')
   if name=='libz':
    for f in ('adler32.c','crc32.c','inffast.c','inflate.c','inftrees.c','uncompr.c'):
     assert 'SOURCE="'+env['EMBER_ZLIB_SOURCE']+'\\'+f+'"' in data,(version,f)
    assert 'SOURCE=.\\zalloc.c' in data
  print(version,'PASS')
