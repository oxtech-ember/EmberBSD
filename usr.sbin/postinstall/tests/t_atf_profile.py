#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the real ATF check/fix with minimal and installed configurations."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', required=True, type=Path)
parser.add_argument('--host-prefix', required=True, type=Path)
args = parser.parse_args()
prefix = args.host_prefix.resolve(strict=True)
shell = (prefix / 'bin/mksh').resolve(strict=True)
assert shell.is_relative_to(prefix)
text = (args.source / 'usr.sbin/postinstall/postinstall.in').read_text()

def function(name):
    start = text.index('\n' + name + '()\n{') + 1
    return text[start:text.index('\n}\n', start) + 3]

program = 'err() { exit "$1"; }\n' + '\n'.join(function(name) for name in (
    'msg', 'check_dir', 'populate_dir', 'find_file_in_dirlist', 'handle_atf_user', 'do_atf'))
program += '\ndo_atf "$OP"\n'
with tempfile.TemporaryDirectory(prefix='postinstall-atf-') as work:
    root = Path(work)
    source, dest = root / 'source', root / 'dest'
    (source / 'etc').mkdir(parents=True)
    atf = dest / 'etc/atf'
    atf.mkdir(parents=True)
    env = dict(PATH=str(prefix / 'bin') + os.pathsep + str(prefix / 'gnu/bin'),
               SRC_DIR=str(source), DEST_DIR=str(dest), SED=str(prefix / 'bin/nbsed'),
               GREP=str(prefix / 'gnu/bin/grep'), RM=str(prefix / 'gnu/bin/rm'),
               DIFF_STYLE='', DIFF_OPT='')
    def run(op='check', success=True):
        result = subprocess.run([str(shell), '-c', program], env=dict(env, OP=op),
                                capture_output=True, text=True)
        assert (result.returncode == 0) == success, (op, result.stdout, result.stderr)
        return result
    # mtree creates this empty directory even when MKATF=no.
    run(); run('fix')
    assert not list(atf.iterdir())
    # Kyua may provide atf-run without the optional ATF subsystem.
    runner = dest / 'usr/bin/atf-run'
    runner.parent.mkdir(parents=True); runner.write_text('kyua compatibility runner\n')
    run()
    helper = dest / 'usr/libexec/atf-check'
    helper.parent.mkdir(parents=True); helper.write_text('ATF helper\n')
    assert "Can't find source directory for NetBSD.conf" in run(success=False).stdout
    run('fix', success=False)
    helper.unlink()
    # A broken installed helper or existing configuration must not bypass checks.
    helper.symlink_to('missing-atf-check')
    run(success=False); helper.unlink()
    for name in ('usr/bin/atf-sh', 'usr/share/atf/libatf-sh.subr'):
        partial = dest / name
        partial.parent.mkdir(parents=True, exist_ok=True)
        partial.write_text('partial ATF runtime\n')
        run(success=False); run('fix', success=False)
        partial.unlink()
    (atf / 'common.conf').write_text('unprivileged-user = _tests\n')
    run(success=False); (atf / 'common.conf').unlink()
    incoming = source / 'etc/atf'
    incoming.mkdir(); (incoming / 'atf-run.hooks').write_text('# hooks\n')
    run(success=False)  # An incomplete etc set still fails.
    (incoming / 'NetBSD.conf').write_text('# NetBSD defaults\n')
    (incoming / 'common.conf').write_text('unprivileged-user = _tests\n')
    run(success=False)
    run('fix'); run()
    assert {p.name for p in atf.iterdir()} == {'NetBSD.conf', 'common.conf', 'atf-run.hooks'}
    (atf / 'common.conf').write_text('unprivileged-user = _atf\n')
    assert 'should be' in run(success=False).stdout
    run('fix'); run()
    assert '_tests' in (atf / 'common.conf').read_text()
    # Preserve the full OS-source layout as well as extracted etc sets.
    for path in incoming.iterdir(): path.unlink()
    native = source / 'external/bsd/atf/etc/atf'; native.mkdir(parents=True)
    sample = source / 'external/bsd/atf/dist/tools/sample'; sample.mkdir(parents=True)
    for name in ('NetBSD.conf', 'common.conf'):
        (native / name).write_text((atf / name).read_text())
    (sample / 'atf-run.hooks').write_text('# hooks\n')
    run()
print('PASS: postinstall ATF optional profile, installed checks, repair and user migration')
