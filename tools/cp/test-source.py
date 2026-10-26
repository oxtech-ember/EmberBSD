#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Canonical cp host contract controls. These do not execute native programs."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CpSourceTests(unittest.TestCase):
    def test_normal_configuration_precedes_native_headers(self):
        for name in ('cp.c', 'utils.c'):
            text = (ROOT / 'bin/cp' / name).read_text()
            self.assertIn('#include "nbtool_config.h"', text)
            self.assertLess(text.index('#include "nbtool_config.h"'), text.index('#include <sys/cdefs.h>'))

    def test_fd_zero_is_not_the_path_sentinel(self):
        text = (ROOT / 'bin/cp/utils.c').read_text()
        body = text[text.index('set_owner_mode(struct stat *fs, int fd)'):text.index('#if !HAVE_NBTOOL_CONFIG_H && !defined(SMALL)')]
        self.assertNotIn('fd ? ', body)
        self.assertNotIn('fd == 0', body)
        self.assertIn('fd >= 0', body)
        for name in ('cp.c', 'utils.c'):
            text = (ROOT / 'bin/cp' / name).read_text()
            self.assertNotIn('setfile(curr->fts_statp, 0)', text)
            self.assertNotIn('setfile(p->fts_statp, 0)', text)
            self.assertNotIn('setfile(from_stat, 0)', text)

    def test_host_metadata_calls_shared_owner_and_keeps_native_body(self):
        text = (ROOT / 'bin/cp/utils.c').read_text()
        self.assertIn('__nbcompat_copy_filemeta_fd(', text)
        self.assertIn('__nbcompat_copy_filemeta_nofollow(', text)
        self.assertIn('fcpxattr(from_fd, to_fd)', text)
        self.assertIn('preserve_fd_acls(int source_fd, int dest_fd)', text)
        self.assertNotIn('#define SMALL', text)
        self.assertNotIn('copyfile(', text)


    def test_host_preservation_uses_fd_zero_and_ordered_public_operations(self):
        text = (ROOT / 'bin/cp/utils.c').read_text()
        body = text.split('preserve_host(FTSENT *entp, int from_fd, int to_fd)', 1)[1]
        body = body.split('#if !HAVE_NBTOOL_CONFIG_H && !defined(SMALL)', 1)[0]
        order = ['set_owner_mode(fs, to_fd)', '__nbcompat_copy_filemeta_fd(',
                 'futimens(to_fd, times)', 'set_flags(fs, to_fd, islink)', 'close(to_fd)']
        self.assertEqual([body.index(token) for token in order], sorted(body.index(token) for token in order))
        self.assertIn('from_fd >= 0', body)
        self.assertIn('O_RDONLY | O_DIRECTORY | O_NOFOLLOW', body)
        self.assertIn('actual.st_dev != fs->st_dev || actual.st_ino != fs->st_ino', body)
        self.assertNotIn('fs->st_atimespec', body)
        self.assertNotIn('fs->st_mtimespec', body)
        before = text.split('copy_link(FTSENT *p, int exists)', 1)[0]
        self.assertLess(before.index('preserve_host(entp, from_fd, to_fd)'),
                        before.index('if (close(from_fd))'))

    def test_normal_recipe_and_control_do_not_substitute_features(self):
        make = (ROOT / 'tools/cp/Makefile').read_text()
        self.assertIn('bin/cp', make)
        self.assertIn('../Makefile.host', make)
        self.assertNotIn('NOCOMPATLIB', make)
        self.assertNotIn('SMALL', make)
        control = (ROOT / 'tools/cp/probes/control.c').read_text()
        self.assertIn('from != STDIN_FILENO', control)
        self.assertIn('fchflags(to, st.st_flags)', control)
        self.assertIn('errno != EBADF', control)
        self.assertNotIn('#define HAVE_', control)
        self.assertNotIn('system(', control)


if __name__ == '__main__':
    unittest.main()
