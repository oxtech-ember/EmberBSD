#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check the shared metadata source contract. No C or configure execution."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPAT = ROOT / 'tools/compat'


class FilemetaSourceTests(unittest.TestCase):
    def test_normal_and_finite_metadata_share_the_complete_source_fragment(self):
        manifest = COMPAT / 'bootstrap-filemeta.json'
        self.assertTrue(manifest.is_file(), 'the common metadata source contract is missing')
        value = json.loads(manifest.read_text())
        self.assertEqual(value['name'], 'filemeta')
        self.assertEqual(value['sources'], ['tools/compat/compat_filemeta.c'])
        self.assertEqual(value['source_fragment'], 'tools/compat/filemeta.mk')
        paths = [value['source_makefile'], value['source_fragment'], *value['sources'],
                 *value['headers'], *value['provenance'], value['control'], value['check'],
                 *(probe['source'] for probe in value['probes'])]
        self.assertEqual(len(paths), len(set(paths)))
        for name in paths:
            self.assertTrue((ROOT / name).is_file(), name)
            self.assertFalse((ROOT / name).is_symlink(), name)
        make = (COMPAT / 'Makefile').read_text()
        self.assertIn('.include "${.CURDIR}/filemeta.mk"', make)
        self.assertIn('SRCS+=\t\t${COMPAT_FILEMETA_SRCS}', make)
        self.assertIn('compat_filemeta.c', (COMPAT / 'filemeta.mk').read_text())

    def test_normal_configuration_uses_exact_finite_probes_without_presets(self):
        value = json.loads((COMPAT / 'bootstrap-filemeta.json').read_text())
        ac = (COMPAT / 'configure.ac').read_text()
        generated = (COMPAT / 'configure').read_text()
        template = (COMPAT / 'nbtool_config.h.in').read_text()
        for row in value['probes']:
            name, macro = row['name'], row['macro']
            include = '#include "$srcdir/probes/' + name + '.c"'
            self.assertEqual(ac.count(include), 1)
            self.assertEqual(generated.count(include), 1)
            self.assertIn('AC_DEFINE([' + macro + '], [1]', ac)
            self.assertIn('#define ' + macro + ' 1', generated)
            self.assertIn('#undef ' + macro, template)
            self.assertNotIn('#define ' + macro, value['config_preamble'] + value['config_postamble'])
            cache = 'ac_cv_' + name.replace('-', '_')
            block = generated.split('checking for ' + name + ' native declarations and link', 1)[1]
            block = block.split('#define ' + macro + ' 1', 1)[0]
            self.assertIn('if ac_fn_c_try_link "$LINENO"', block)
            self.assertIn(cache + '=yes', block)
            self.assertIn(cache + '=no', block)
        self.assertEqual(len(value['probes']), 8)

    def test_metadata_feature_hook_precedes_normal_compatibility_headers(self):
        hook = ('#if defined(EMBERBSD_COMPAT_FILEMETA_NATIVE_HEADERS)\n'
                '#include "compat_filemeta_features.h"\n#endif\n')
        for name in ('configure.ac', 'nbtool_config.h.in'):
            text = (COMPAT / name).read_text()
            self.assertIn(hook, text)
            self.assertLess(text.index(hook), text.index('#include "compat_defs.h"'))
        source = (COMPAT / 'compat_filemeta.c').read_text()
        self.assertIn('#define EMBERBSD_COMPAT_FILEMETA_NATIVE_HEADERS 1\n'
                      '#include "nbtool_config.h"\n#undef EMBERBSD_COMPAT_FILEMETA_NATIVE_HEADERS', source)
        feature = (COMPAT / 'compat_filemeta_features.h').read_text()
        self.assertLess(feature.index('#include "compat_native_features.h"'),
                        feature.index('#define _NETBSD_SOURCE 1'))
        self.assertIn('#if HAVE_FILEMETA_ACL_BSD || defined(EMBERBSD_FILEMETA_ACL_PROBE)\n'
                      '#include "compat_acl_features.h"\n#endif', feature)
        for name in ('filemeta-open.c', 'filemeta-regular-open.c', 'filemeta-xattr-bsd-probe.h'):
            probe = (COMPAT / 'probes' / name).read_text()
            self.assertNotIn('sys/acl.h', probe)
            self.assertNotIn('#define EMBERBSD_FILEMETA_ACL_PROBE', probe)
        self.assertNotIn('sys/acl.h', (COMPAT / 'compat_native_features.h').read_text())

    def test_listing_and_metadata_use_one_darwin_acquisition_owner(self):
        for name in ('compat_acl.c', 'compat_filemeta.c'):
            source = (COMPAT / name).read_text()
            self.assertIn('#include "compat_native_acl.h"', source)
            self.assertIn('__nbcompat_darwin_acl_read(', source)
            self.assertIn('__nbcompat_darwin_acl_nonempty(', source)
            self.assertNotIn('filesec_init', source)
        shared = (COMPAT / 'compat_native_acl.h').read_text()
        self.assertEqual(shared.count('filesec_init()'), 1)
        self.assertIn('filesec_query_property', shared)
        self.assertNotIn('errno == ENOENT', shared)

    def test_path_open_requires_real_type_gates_and_never_uses_event_only(self):
        source = (COMPAT / 'compat_filemeta.c').read_text()
        body = source.split('path_endpoint(', 1)[1].split('static ssize_t', 1)[0]
        self.assertIn('flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC', body)
        self.assertIn('flags |= O_DIRECTORY', body)
        self.assertIn('#if HAVE_FILEMETA_REGULAR_OPEN', body)
        self.assertIn('flags |= O_REGULAR', body)
        self.assertLess(body.index('errno = ENOTSUP'), body.index('open(path, flags)'))
        self.assertNotIn('O_EVTONLY', source)
        self.assertNotIn('O_NONBLOCK', source)
        self.assertNotRegex(source, r'#define\s+(?:O_|UF_|SF_)')

    def test_bounds_and_failure_policy_remain_explicit(self):
        source = (COMPAT / 'compat_filemeta.c').read_text()
        self.assertIn('attempt < 3', source)
        self.assertIn('(uintmax_t)size > PTRDIFF_MAX', source)
        self.assertIn('replacement = realloc(buffer,', source)
        self.assertIn('namesize > length - offset - 1', source)
        self.assertIn('memchr(item,', source)
        self.assertIn('(size_t)written != size', source)
        self.assertNotIn('errno == EOPNOTSUPP', source)
        self.assertNotIn('errno == EPERM', source)
        self.assertNotIn('errno == EACCES', source)
        for forbidden in ('_ACL_PRIVATE', 'ats_acl', 'acl_cnt', 'copyfile(', 'unlink(', 'fprintf('):
            self.assertNotIn(forbidden, source)
        body = source.split('copy_metadata(', 1)[1].split('static int\nvalid_parts', 1)[0]
        self.assertLess(body.index('copy_attributes('), body.index('copy_acl('))

    def test_symlink_acl_write_never_uses_a_following_darwin_setter(self):
        source = (COMPAT / 'compat_filemeta.c').read_text()
        darwin = source.split('#if HAVE_FILEMETA_ACL_DARWIN\nstatic int', 1)[1]
        darwin = darwin.split('#elif HAVE_FILEMETA_ACL_BSD', 1)[0]
        self.assertIn('if (!present)', darwin)
        self.assertIn('if (to->fd < 0)', darwin)
        self.assertIn('errno = ENOTSUP', darwin)
        self.assertIn('acl_set_fd_np(to->fd, acl, ACL_TYPE_EXTENDED)', darwin)
        self.assertNotIn('acl_set_link_np', darwin)
        self.assertNotIn('acl_set_file', darwin)
        self.assertIn('trivial = entries == 0', source)
        self.assertIn('acl_get_entry(acl, ACL_FIRST_ENTRY, &entry)', source)

    def test_native_check_uses_independent_getters_and_retains_source_fixtures(self):
        check = (COMPAT / 'probes/filemeta-check.c').read_text()
        for call in ('fgetxattr(', 'acl_get_fd_np(', 'acl_to_text(', 'fstatfs(', 'dup2('):
            self.assertIn(call, check)
        for case in ('ACL_ENTRY_FILE_INHERIT', 'ACL_ENTRY_DIRECTORY_INHERIT',
                     'filemeta-source-link', 'filemeta-special', 'errno == ENOTSUP',
                     'errno == EINVAL', 'errno == EBADF', 'errno == ENOENT'):
            self.assertIn(case, check)
        self.assertIn('before.st_ino == after.st_ino', check)
        self.assertIn('value_equals(dst, retained, "keep", 4)', check)

    def test_public_header_install_does_not_clean_source_headers(self):
        make = (COMPAT / 'Makefile').read_text()
        installed = re.search(r'^INCFILES=\s*(.*)$', make, re.M).group(1).split()
        self.assertIn('compat_filemeta.h', installed)
        clean = '\n'.join(x for x in make.splitlines() if x.startswith(('CLEANFILES', 'CLEANDIRFILES')))
        self.assertNotIn('${INCFILES}', clean)
        self.assertNotIn('compat_filemeta.h', clean)


if __name__ == '__main__':
    unittest.main()
