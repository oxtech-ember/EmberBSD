#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check a selected native mv in fresh owned directories. No compiler runs."""
import argparse
import errno
import json
import os
from pathlib import Path
import stat
import subprocess
import sys


def fresh(value):
    path = Path(value)
    if not path.is_absolute() or path.exists() or path.is_symlink():
        raise ValueError('Select an absolute fresh work directory')
    if path.parent.resolve(strict=True) != path.parent:
        raise ValueError('The work parent must be canonical')
    if sys.platform == 'darwin' and not any(part.endswith('.noindex') for part in path.parts):
        raise ValueError('Use .noindex test storage on Darwin to reduce indexing interference')
    path.mkdir(mode=0o700)
    return path


def regular(path, data=b'move data\n', mode=0o640):
    path.write_bytes(data)
    path.chmod(mode)


def check(program, work, other):
    results = []

    def run(name, args, expected=0, stdin=b''):
        result = subprocess.run([str(program), *map(str, args)], cwd=work,
                                input=stdin, capture_output=True, timeout=20)
        results.append(dict(case=name, argv=list(map(str, args)),
                            actual_exit=result.returncode,
                            stdout=result.stdout.decode(errors='backslashreplace'),
                            stderr=result.stderr.decode(errors='backslashreplace')))
        (work / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        assert result.returncode == expected, results[-1]
        if expected:
            assert result.stderr, results[-1]
        return result

    regular(work / 'a')
    run('file', ['a', 'b'])
    assert not (work / 'a').exists() and (work / 'b').read_bytes() == b'move data\n'
    regular(work / 'a', b'replacement')
    run('overwrite', ['a', 'b'])
    assert (work / 'b').read_bytes() == b'replacement'
    run('same-path', ['b', 'b'])
    assert (work / 'b').read_bytes() == b'replacement'
    result = run('verbose', ['-v', 'b', 'a'])
    assert result.stdout == b'b -> a\n'
    (work / 'dir').mkdir()
    regular(work / 'dir' / 'child')
    inode = (work / 'dir').stat().st_ino
    run('tree-rename', ['dir', 'renamed'])
    assert (work / 'renamed').stat().st_ino == inode
    assert (work / 'renamed' / 'child').read_bytes() == b'move data\n'
    (work / 'link').symlink_to('missing')
    run('dangling-link', ['link', 'moved-link'])
    assert os.readlink(work / 'moved-link') == 'missing'
    (work / 'target').mkdir()
    (work / 'dir-link').symlink_to('target')
    regular(work / 'inside')
    run('destination-directory-link', ['inside', 'dir-link'])
    assert (work / 'target' / 'inside').is_file()
    regular(work / 'replace-link')
    run('replace-directory-link-h', ['-h', 'replace-link', 'dir-link'])
    assert (work / 'dir-link').is_file() and not (work / 'dir-link').is_symlink()
    for answer, moved in ((b'n\n', False), (b'y\n', True)):
        regular(work / 'source', b'new')
        regular(work / 'dest', b'old')
        result = run('interactive-' + answer[:1].decode(), ['-i', 'source', 'dest'], stdin=answer)
        assert result.stderr == b'overwrite dest? '
        assert (work / 'source').exists() == (not moved)
        assert (work / 'dest').read_bytes() == (b'new' if moved else b'old')
    regular(work / 'source')
    result = run('force-after-interactive', ['-i', '-f', 'source', 'dest'])
    assert not result.stderr and not (work / 'source').exists()
    regular(work / 'source')
    run('interactive-after-force', ['-f', '-i', 'source', 'dest'], stdin=b'n\n')
    assert (work / 'source').exists()
    regular(work / '-operand')
    run('option-terminator', ['--', '-operand', 'dash'])
    assert (work / 'dash').is_file()
    regular(work / 'one')
    regular(work / 'two')
    run('multiple-operands', ['one', 'two', 'target'])
    assert all((work / 'target' / n).is_file() for n in ('one', 'two'))
    for name, args in (('no-operands', []), ('one-operand', ['source']),
                       ('unknown-option', ['-Q']), ('missing-source', ['absent', 'out']),
                       ('missing-parent', ['source', 'absent/out'])):
        run(name, args, 1)
        assert (work / 'source').exists()
    regular(work / 'timed', b'time', 0o751)
    os.utime(work / 'timed', ns=(1709210096123456789, 1709210096987654321))
    before = (work / 'timed').stat()
    run('rename-metadata', ['timed', 'time-moved'])
    after = (work / 'time-moved').stat()
    assert (after.st_ino, after.st_mode, after.st_atime_ns, after.st_mtime_ns) == (
        before.st_ino, before.st_mode, before.st_atime_ns, before.st_mtime_ns)
    if other is None:
        return dict(cases=results, real_exdev=False,
                    limitations=['No second filesystem was selected; EXDEV and nested cp/rm were not tested.'])
    if work.stat().st_dev == other.stat().st_dev:
        raise ValueError('EXDEV checks require different real device IDs')
    regular(work / 'rename-probe')
    try:
        os.rename(work / 'rename-probe', other / 'rename-probe')
    except OSError as error:
        if error.errno != errno.EXDEV:
            raise
    else:
        raise AssertionError('The real cross-device rename did not report EXDEV')
    assert (work / 'rename-probe').exists() and not (other / 'rename-probe').exists()
    for size in (0, 1, 8192, 8193):
        source, destination = work / ('cross-' + str(size)), other / ('cross-' + str(size))
        regular(source, b'x' * size, 0o751)
        os.utime(source, ns=(1709210096123456789, 1709210096987654321))
        before = source.stat()
        run('exdev-regular-' + str(size), [source, destination])
        after = destination.stat()
        assert not source.exists()
        assert (stat.S_IMODE(after.st_mode), after.st_atime_ns, after.st_mtime_ns) == (
            0o751, before.st_atime_ns, before.st_mtime_ns)
        assert destination.read_bytes() == b'x' * size
    (work / 'cross-tree').mkdir()
    regular(work / 'cross-tree/child')
    (work / 'cross-tree/link').symlink_to('child')
    run('exdev-tree', [work / 'cross-tree', other / 'cross-tree'])
    assert not (work / 'cross-tree').exists()
    assert (other / 'cross-tree/child').read_bytes() == b'move data\n'
    assert os.readlink(other / 'cross-tree/link') == 'child'
    (work / 'cross-link').symlink_to('missing')
    run('exdev-dangling-link', [work / 'cross-link', other / 'cross-link'])
    assert not (work / 'cross-link').is_symlink()
    assert os.readlink(other / 'cross-link') == 'missing'
    if os.geteuid() == 0:
        raise ValueError('The real unreadable-file refusal requires an unprivileged user')
    (work / 'copy-failure').mkdir()
    regular(work / 'copy-failure/unreadable', b'keep', 0)
    try:
        run('exdev-child-copy-failure', [work / 'copy-failure', other / 'copy-failure'], 1)
        assert (work / 'copy-failure/unreadable').is_file()
    finally:
        (work / 'copy-failure/unreadable').chmod(0o600)
    assert (work / 'copy-failure/unreadable').read_bytes() == b'keep'
    return dict(cases=results, real_exdev=True,
                limitations=['ACL/xattr contents and child loader routing need separate observations.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, required=True)
    parser.add_argument('--work', required=True)
    parser.add_argument('--other-work')
    args = parser.parse_args()
    if not args.program.is_absolute() or not os.access(args.program, os.X_OK):
        parser.error('Select an absolute executable SDK mv')
    work = fresh(args.work)
    other = fresh(args.other_work) if args.other_work else None
    result = check(args.program, work, other)
    (work / 'final-result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(cases=len(result['cases']), real_exdev=result['real_exdev'])))
