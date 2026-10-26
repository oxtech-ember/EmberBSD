#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Source checks; no native mv is executed.
from pathlib import Path
import re
import unittest

SOURCE = Path(__file__).with_name('mv.c')


def fastcopy(source):
    return source.split('\nfastcopy(', 1)[1].split('\nstatic int\ncopy(', 1)[0]


def check_characteristics_policy(source):
    """Keep canonical metadata policy separate from data or close failures."""
    text = fastcopy(source)
    expected = (
        'if (fcpxattr(from_fd, to_fd) == -1)\n'
        '\t\twarn("%s: error copying extended attributes", to);',
        'if (fchown(to_fd, sbp->st_uid, sbp->st_gid)) {\n'
        '\t\tif (errno != EPERM)\n\t\t\twarn("%s: set owner/group", to);\n'
        '\t\tsbp->st_mode &= ~(S_ISUID | S_ISGID);\n\t}',
        'if (fchmod(to_fd, sbp->st_mode))\n\t\twarn("%s: set mode", to);',
        'if (fchflags(to_fd, sbp->st_flags) && (errno != EOPNOTSUPP))\n'
        '\t\twarn("%s: set flags (was: 0%07o)", to, sbp->st_flags);')
    if not all(block in text for block in expected):
        raise ValueError('canonical warning and ownership-mode policy changed')
    metadata = text[text.index('if (fcpxattr('):text.index('if (close(to_fd))')]
    metadata = re.sub(r'if \(close\(from_fd\)\)\s*\{[^{}]+\}', '', metadata)
    if 'return ' in metadata or 'goto ' in metadata:
        raise ValueError('characteristic warnings must not become fatal copy errors')
    times = text[text.index('#ifdef BSD4_4'):text.index('if (fchown(')]
    if 'goto ' in times or 'return ' in times:
        raise ValueError('timestamp characteristic failure must not change exit status')
    if 'warn("%s: set times", to);' not in times:
        raise ValueError('timestamp failure diagnostic missing')
    for call in ('utimes(to, tval)', 'futimens(to_fd, ts)', 'futimes(to_fd, tval)'):
        if 'if (' + call + ')' not in times:
            raise ValueError('existing timestamp backend removed')


def check_copy_errors(source):
    text = fastcopy(source)
    remove = text.index('if (unlink(from))')
    for descriptor in ('from_fd', 'to_fd'):
        match = re.search(r'if \(close\(' + descriptor + r'\)\)\s*\{([^{}]+)\}', text)
        if match is None or match.start() >= remove or 'return (1);' not in match[1]:
            raise ValueError('a close failure must stop before source removal')
    source_close = text[text.index('if (close(from_fd))'):text.index('#ifdef BSD4_4')]
    if '(void)close(to_fd);' not in source_close:
        raise ValueError('source-close failure must release the destination descriptor')
    cleanup = text[text.index('err:'):text.index('if (fcpxattr(')]
    if 'unlink(to)' not in cleanup or 'unlink(from)' in cleanup or 'return (1);' not in cleanup:
        raise ValueError('data-copy failure cleanup must preserve the source')
    if 'if (nread < 0)' not in text or 'if (write(to_fd, bp, nread) != nread)' not in text:
        raise ValueError('read and short-write failure paths missing')


class MoveSourceTests(unittest.TestCase):
    def test_characteristics_keep_canonical_policy(self):
        check_characteristics_policy(SOURCE.read_text())

    def test_data_and_close_errors_keep_source(self):
        check_copy_errors(SOURCE.read_text())

    def test_strict_metadata_and_mode_mutants_refuse(self):
        source = SOURCE.read_text()
        check_characteristics_policy(source)
        for diagnostic in ('error copying extended attributes', 'set times',
                           'set owner/group', 'set mode', 'set flags'):
            pos = source.index(diagnostic, source.index('\nfastcopy('))
            end = source.index(';', pos) + 1
            mutant = source[:end] + '\n\t\treturn (1);' + source[end:]
            with self.subTest(diagnostic=diagnostic), self.assertRaises(ValueError):
                check_characteristics_policy(mutant)
        with self.assertRaises(ValueError):
            check_characteristics_policy(source.replace('sbp->st_mode &= ~(S_ISUID | S_ISGID);', ''))

    def test_ignored_close_and_wrong_cleanup_mutants_refuse(self):
        source = SOURCE.read_text()
        check_copy_errors(source)
        for descriptor in ('from_fd', 'to_fd'):
            pos = source.index('if (close(' + descriptor + '))', source.index('\nfastcopy('))
            end = source.index('return (1);', pos)
            mutant = source[:end] + source[end:].replace('return (1);', '', 1)
            with self.subTest(descriptor=descriptor), self.assertRaises(ValueError):
                check_copy_errors(mutant)
        with self.assertRaises(ValueError):
            check_copy_errors(source.replace('err:\t\tif (unlink(to))', 'err:\t\tif (unlink(from))'))


if __name__ == '__main__':
    unittest.main()
