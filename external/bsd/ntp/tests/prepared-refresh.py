#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). NTP prepared-source refresh contract.
"""Check the NTP refresh plan and its keyword-generator cleanup."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

p = argparse.ArgumentParser(description=__doc__)
for name in ('sdk-source', 'bmake', 'tools', 'ntp-source', 'libopts-source', 'work-dir'):
    p.add_argument('--' + name, required=True, type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(a.sdk_source / 'src'))
from emberbsd_sdk import prepared_refresh

expected = {'external/bsd/ntp/' + suffix for suffix in (
    'lib/libntp', 'lib/libiscntp', 'lib/libopts', 'lib/libparse',
    'bin/ntp-keygen', 'bin/ntpd', 'bin/ntpdate', 'bin/ntpdc',
    'bin/ntpq', 'bin/ntptime', 'bin/ntptrace', 'bin/sntp', 'html')}
with tempfile.TemporaryDirectory(prefix='ntp-refresh-', dir=a.work_dir) as tmp:
    objects = Path(tmp) / 'objects'
    for name in expected:
        (objects / name).mkdir(parents=True)
    unrelated = objects / 'unrelated/sentinel'
    unrelated.parent.mkdir(parents=True)
    unrelated.write_text('keep')
    ntpd = objects / 'external/bsd/ntp/bin/ntpd'
    generated = ('ntp-keyword-gen', 'ntp_keyword.h', 'ntp_keyword.h.tmp')
    for name in generated:
        (ntpd / name).write_text('old')
    with mock.patch.object(prepared_refresh.os, 'walk', side_effect=AssertionError('unexpected global object scan')):
        actual = prepared_refresh.plan(root, objects, {'ntp'})
    assert set(actual) == expected, (actual, expected)
    assert unrelated.read_text() == 'keep'
    assert all((ntpd / name).is_file() for name in generated)
    # Run the owner's actual cleandir only against the temporary ntpd objdir.
    env = dict(os.environ, MAKEOBJDIR=str(ntpd))
    env.pop('MAKEOBJDIRPREFIX', None)
    env['PATH'] = str(a.bmake.parent) + ':' + str(a.bmake.parent.parent / 'gnu/bin') + ':' + str(a.tools / 'bin')
    command = [str(a.bmake), '-m', str(root / 'share/mk'), '-C', str(root / 'external/bsd/ntp/bin/ntpd'),
               'MACHINE=amd64', 'MACHINE_ARCH=x86_64', 'NETBSDSRCDIR=' + str(root),
               'TOOLDIR=' + str(a.tools), 'EMBER_NTP_SOURCE=' + str(a.ntp_source),
               'EMBER_LIBOPTS_SOURCE=' + str(a.libopts_source), 'MAKECONF=/dev/null', 'MKCLEANSRC=no']
    selected = subprocess.run([*command, '-V', '${.OBJDIR}'], env=env, capture_output=True, text=True, check=True)
    assert Path(selected.stdout.strip()).resolve() == ntpd.resolve(), selected.stdout
    subprocess.run([*command, 'cleandir'], env=env, capture_output=True, text=True, check=True)
    assert all(not (ntpd / name).exists() for name in generated)
    assert unrelated.read_text() == 'keep'
print('PASS: scoped NTP plan, no global scan, keyword generator cleandir and unrelated object retention')
