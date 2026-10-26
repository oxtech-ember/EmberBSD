#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Source checks, not native execution.
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HostSourceTests(unittest.TestCase):
    def test_host_configuration_precedes_native_headers(self):
        text = (ROOT / 'bin/mv/mv.c').read_text()
        self.assertLess(text.index('#include "nbtool_config.h"'),
                        text.index('#include <sys/cdefs.h>'))
        self.assertIn('#include "compat_filemeta.h"', text)
        self.assertIn('#include "compat_stat_time.h"', text)

    def test_selected_children_and_target_paths(self):
        text = (ROOT / 'bin/mv/pathnames.h').read_text()
        self.assertIn('#if HAVE_NBTOOL_CONFIG_H', text)
        self.assertIn('#define\t_PATH_CP\tEMBERBSD_MV_CP', text)
        self.assertIn('#define\t_PATH_RM\tEMBERBSD_MV_RM', text)
        self.assertIn('#elif defined(RESCUEDIR)', text)
        self.assertIn('#define\t_PATH_CP\t"/bin/cp"', text)
        self.assertIn('#define\t_PATH_RM\t"/bin/rm"', text)

    def test_host_recipe_retains_normal_compatibility(self):
        text = (ROOT / 'tools/mv/Makefile').read_text()
        self.assertIn('HOST_SRCDIR=\tbin/mv', text)
        self.assertIn('../Makefile.host', text)
        self.assertNotIn('NOCOMPATLIB', text)
        self.assertIn('HOST_MV_${_mv_child}', text)
        self.assertIn('exists(', text)

    def test_host_metadata_order_and_warning_policy(self):
        text = (ROOT / 'bin/mv/mv.c').read_text()
        begin = text.index('if (fchmod(to_fd, sbp->st_mode))')
        meta = text.index('__nbcompat_copy_filemeta_fd(', begin)
        end = text.index('if (close(from_fd))', meta)
        self.assertLess(begin, meta)
        self.assertIn('NB_FILEMETA_XATTR | NB_FILEMETA_ACL', text[meta:end])
        self.assertNotIn('return ', text[meta:end])
        self.assertNotIn('goto ', text[meta:end])
        self.assertLess(end, text.index('if (fchflags(', end))
        self.assertIn('__nbcompat_stat_atime(sbp)', text)
        self.assertIn('__nbcompat_stat_mtime(sbp)', text)
        self.assertIn('if (futimens(to_fd, ts))', text)


if __name__ == '__main__':
    unittest.main()
