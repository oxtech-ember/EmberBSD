#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check selected chown/chgrp on fresh files owned by the current user."""
import argparse
import grp
import json
import os
from pathlib import Path
import pwd
import subprocess


def check(chown, chgrp, root):
    uid, gid = os.getuid(), os.getgid()
    groups = sorted(set(os.getgroups()) - {gid})
    selected_gid = groups[0] if groups else gid
    user, group = pwd.getpwuid(uid).pw_name, grp.getgrgid(gid).gr_name
    rows = []
    def run(name, tool, args, expected=0):
        p = subprocess.run([str(tool), *map(str, args)], cwd=root,
                           env=dict(os.environ, LC_ALL='C'),
                           capture_output=True, timeout=20)
        rows.append(dict(case=name, argv=list(map(str,args)), actual_exit=p.returncode,
                         stdout=p.stdout.decode(errors='backslashreplace'),
                         stderr=p.stderr.decode(errors='backslashreplace')))
        (root/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
        assert p.returncode == expected, rows[-1]
        if expected: assert p.stderr, rows[-1]
        return p
    file = root/'file'; file.write_bytes(b'keep data\n'); inode=file.stat().st_ino
    run('numeric-owner-group', chown, [f'{uid}:{gid}', 'file'])
    assert (file.stat().st_uid,file.stat().st_gid)==(uid,gid)
    run('named-owner-group', chown, [f'{user}:{group}', 'file'])
    for name, tool, operand in (('nbchown.exe', chown, f'{uid}:{gid}'),
                                ('nbchgrp.exe', chgrp, str(gid)),
                                ('g', chgrp, str(gid))):
        alias = root / name
        alias.symlink_to(tool)
        run('invocation-' + name, alias, [operand, 'file'])
        assert (file.stat().st_uid, file.stat().st_gid) == (uid, gid)
    run('forced-numeric-owner-group', chown, [f'#{uid}:#{gid}', 'file'])
    run('group-alias', chgrp, [str(selected_gid),'file'])
    assert file.stat().st_gid == selected_gid
    run('group-only', chown, [f':{gid}', 'file'])
    assert file.stat().st_gid == gid
    run('named-group-alias', chgrp, [group,'file'])
    reference=root/'reference'; reference.write_bytes(b'reference')
    os.chown(reference,uid,selected_gid)
    run('reference-owner',chown,['--reference=reference','file'])
    assert (file.stat().st_uid,file.stat().st_gid)==(uid,selected_gid)
    run('reference-group',chgrp,['--reference=reference','file'])
    assert file.stat().st_gid==selected_gid
    file.chmod(0o640)
    before=file.stat()
    run('skip-already-correct',chown,['-d',f'{uid}:{selected_gid}','file'])
    assert file.stat().st_ctime_ns==before.st_ctime_ns
    result=run('verbose',chown,['-v',str(uid),'file'])
    assert result.stdout==b'file\n'
    file.chmod(0o640)
    assert file.stat().st_ino==inode and file.read_bytes()==b'keep data\n'
    outside=root/'outside'; outside.write_bytes(b'outside'); os.chown(outside,uid,gid)
    tree=root/'tree'; tree.mkdir(); (tree/'child').write_bytes(b'child')
    (tree/'link').symlink_to('../outside')
    run('recursive-physical',chgrp,['-R','-P',str(selected_gid),'tree'])
    assert (tree/'child').stat().st_gid==selected_gid and outside.stat().st_gid==gid
    run('recursive-logical',chgrp,['-R','-L',str(selected_gid),'tree'])
    assert outside.stat().st_gid==selected_gid
    os.chown(outside,uid,gid)
    os.chown(tree/'child',uid,gid)
    (root/'root-link').symlink_to('tree')
    run('recursive-command-link',chgrp,['-R','-H',str(selected_gid),'root-link'])
    assert (tree/'child').stat().st_gid==selected_gid and outside.stat().st_gid==gid
    (root/'link').symlink_to('outside')
    run('symlink-owner',chown,['-h',f'{uid}:{selected_gid}','link'])
    assert (root/'link').lstat().st_gid==selected_gid and outside.stat().st_gid==gid
    (root/'dangling').symlink_to('missing-target')
    run('dangling-symlink-owner',chown,['-h',f'{uid}:{selected_gid}','dangling'])
    assert (root/'dangling').lstat().st_gid==selected_gid
    for name,args in [('no-operands',[]),('one-operand',[str(uid)]),
                      ('unknown-option',['-Q']),('unknown-owner',['emberbsd-no-such-owner-672983','file']),
                      ('unknown-group',[f'{uid}:emberbsd-no-such-group-672983','file']),
                      ('missing-file',[str(uid),'absent']),
                      ('missing-reference',['--reference=absent','file']),
                      ('conflicting-links',['-R','-L','-h',str(uid),'tree'])]:
        run(name,chown,args,1)
    assert file.exists() and file.read_bytes()==b'keep data\n'
    return dict(cases=len(rows),distinct_group_change=bool(groups),
                limitations=['Only current-user ownership and permitted groups are tested; privileged ownership changes are not tested.'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chown',type=Path,required=True)
    parser.add_argument('--chgrp',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    args=parser.parse_args()
    for p in (args.chown,args.chgrp):
        if not p.is_absolute() or not os.access(p,os.X_OK): parser.error('Select absolute executable tools')
    if not args.work.is_absolute() or args.work.parent.resolve()!=args.work.parent: parser.error('Select a fresh canonical work path')
    args.work.mkdir(mode=0o700)
    result=check(args.chown,args.chgrp,args.work)
    (args.work/'final-result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))
