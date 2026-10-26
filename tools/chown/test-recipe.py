#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check the real host recipe with a bounded make configuration, without compilation."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

SOURCE=Path(__file__).resolve().parents[2]
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bmake','shell','ln','work'): p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for path in (a.bmake,a.shell,a.ln,a.work):
        if not path.is_absolute(): p.error('Use absolute selected tool and work paths')
    a.work.mkdir()
    src=a.work/'source';mk=a.work/'mk';bindir=a.work/'bin'
    mk.mkdir();bindir.mkdir()
    for relative in ('tools/chown/Makefile','tools/Makefile.host','sbin/chown/Makefile','sbin/Makefile.inc'):
        target=src/relative;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(SOURCE/relative,target)
    (mk/'bsd.own.mk').write_text('USETOOLS=yes\nMKDYNAMICROOT=yes\nMKLINT=no\nMKUPDATE=yes\nBINMODE=0555\nHOSTEXEEXT=\n')
    # Only platform and compiler rules are omitted. The owner recipes are exact.
    for name in ('bsd.prog.mk','bsd.hostprog.mk'): (mk/name).write_text('# No compilation in this metadata test.\n')
    cwd=src/'tools/chown'
    (cwd/'chown').write_bytes(b'Metadata fixture; never execute.\n')
    (bindir/'nbchown').write_bytes((cwd/'chown').read_bytes())
    os.utime(cwd/'chown',ns=(1000000000,1000000000))
    os.utime(bindir/'nbchown',ns=(2000000000,2000000000))
    command=[str(a.bmake),'-r','-m',str(mk),'SHELL='+str(a.shell),
             'HOST_SH='+str(a.shell),'_TOOL_PREFIX=nb','HOST_BINDIR='+str(bindir),
             'HOST_INSTALL_SYMLINK='+str(a.ln)+' -sf']
    def run(extra):
        result=subprocess.run(command+extra,cwd=cwd,env=dict(os.environ,PATH=str(a.ln.parent)),
                              capture_output=True,text=True,timeout=20)
        assert result.returncode==0,result.stdout+result.stderr
        return result.stdout
    assert run(['-V','${SYMLINKS}']).strip()=='','Host recipe retains target filesystem aliases'
    assert run(['-V','${HOSTPROGNAME}']).strip()=='nbchown'
    run(['install.host'])
    assert os.readlink(bindir/'nbchgrp')=='nbchown'
    assert (bindir/'nbchgrp').read_bytes()==(cwd/'chown').read_bytes()
    print('Host install alias and target-alias isolation passed; no compiler ran.')
