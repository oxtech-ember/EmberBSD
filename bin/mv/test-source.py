#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Source checks; no native mv is executed.
from pathlib import Path
import re
import unittest

SOURCE = Path(__file__).with_name('mv.c')


def fastcopy(source):
    return source.split('\nfastcopy(', 1)[1].split('\nstatic int\ncopy(', 1)[0]


def check_required_metadata(text):
    text = fastcopy(text)
    for operation, diagnostic in (
            ('fcpxattr', 'error copying extended attributes'),
            ('fchown', 'set owner/group'), ('fchmod', 'set mode'),
            ('fchflags', 'set flags')):
        branch = re.search(r'if \(' + operation + r'\([^\n]+\)\s*\{([^{}]+)\}', text)
        if branch is None or diagnostic not in branch[1] or 'goto err;' not in branch[1]:
            raise ValueError(operation + ' failure must preserve the source')
        if 'errno !=' in branch[0]:
            raise ValueError(operation + ' must not hide an actual metadata failure')
    if not re.search(r'\{\s*warn\("%s: set times", to\);\s*goto err;\s*\}', text):
        raise ValueError('all timestamp backends must share the failure path')
    for call in ('utimes(to, tval)', 'futimens(to_fd, ts)', 'futimes(to_fd, tval)'):
        if 'if (' + call + ')' not in text:
            raise ValueError('existing timestamp backend was removed')
    metadata_end = text.index('if (fchflags(')
    close_source = text.index('if (close(from_fd))')
    close_dest = text.index('if (close(to_fd))')
    remove_source = text.index('if (unlink(from))')
    if not metadata_end < close_source < close_dest < remove_source:
        raise ValueError('source deletion requires completed metadata and closes')
    for descriptor in ('from_fd', 'to_fd'):
        branch = re.search(r'if \(close\(' + descriptor + r'\)\)\s*\{([^{}]+)\}', text)
        if branch is None or 'return (1);' not in branch[1]:
            raise ValueError('close failure must preserve the source')
    cleanup = text[text.index('err:'):text.index('if (fcpxattr(')]
    if 'unlink(to)' not in cleanup or 'unlink(from)' in cleanup or 'return (1);' not in cleanup:
        raise ValueError('failed copy cleanup may remove only the destination')


class MoveSourceTests(unittest.TestCase):
    def test_required_metadata_and_close_failures_keep_source(self):
        check_required_metadata(SOURCE.read_text())

    def test_warn_only_metadata_mutants_are_rejected(self):
        source = SOURCE.read_text()
        check_required_metadata(source)
        for diagnostic in ('error copying extended attributes', 'set times',
                           'set owner/group', 'set mode', 'set flags'):
            pos = source.index(diagnostic, source.index('\nfastcopy('))
            stop = source.index('goto err;', pos)
            mutant = source[:stop] + source[stop:].replace('goto err;', '', 1)
            with self.subTest(diagnostic=diagnostic), self.assertRaises(ValueError):
                check_required_metadata(mutant)

    def test_close_and_errno_exception_mutants_are_rejected(self):
        source = SOURCE.read_text()
        check_required_metadata(source)
        for descriptor in ('from_fd', 'to_fd'):
            pos = source.index('if (close(' + descriptor + '))', source.index('\nfastcopy('))
            stop = source.index('return (1);', pos)
            mutant = source[:stop] + source[stop:].replace('return (1);', '', 1)
            with self.subTest(descriptor=descriptor), self.assertRaises(ValueError):
                check_required_metadata(mutant)
        mutant = source.replace('if (fchflags(to_fd, sbp->st_flags))',
                                'if (fchflags(to_fd, sbp->st_flags) && (errno != EOPNOTSUPP))')
        with self.assertRaises(ValueError):
            check_required_metadata(mutant)


if __name__ == '__main__':
    unittest.main()
