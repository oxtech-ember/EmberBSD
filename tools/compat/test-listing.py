#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Source-only contracts; no C programs or configure processes are run."""
import hashlib
import json
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPAT = ROOT / 'tools/compat'

class ListingSourceTests(unittest.TestCase):
    def test_acl_native_headers_use_one_scoped_netbsd_adapter(self):
        source = (COMPAT / 'compat_acl.c').read_text()
        self.assertIn('#define EMBERBSD_COMPAT_ACL_NATIVE_HEADERS 1\n#include "nbtool_config.h"\n#undef EMBERBSD_COMPAT_ACL_NATIVE_HEADERS', source)
        hook = ('#if defined(EMBERBSD_COMPAT_ACL_NATIVE_HEADERS) && HAVE_LISTING_ACL_BSD\n'
                '#include "compat_acl_features.h"\n#endif\n')
        for name in ('configure.ac', 'nbtool_config.h.in'):
            text = (COMPAT / name).read_text()
            self.assertIn(hook + '#include "compat_defs.h"', text)
        probe = (COMPAT / 'probes/listing-acl-bsd.c').read_text()
        self.assertEqual(re.search(r'^#include (.*)$', probe, re.M).group(1),
                         '"compat_acl_features.h"')
        value = json.loads((COMPAT / 'bootstrap-listing.json').read_text())
        self.assertIn(hook, value['config_postamble'])
        self.assertIn('tools/compat/compat_acl_features.h', value['headers'])
        adapter = (COMPAT / 'compat_acl_features.h').read_text()
        # Restricted types/endian must precede the local feature expansion.
        start = adapter.index('#define _NETBSD_SOURCE 1')
        end = adapter.index('#undef _NETBSD_SOURCE')
        for header in ('sys/types.h', 'sys/endian.h', 'sys/time.h', 'sys/param.h'):
            self.assertLess(adapter.index('#include <' + header + '>'), start)
        for header in ('sys/stat.h', 'sys/acl.h', 'unistd.h'):
            pos = adapter.index('#include <' + header + '>')
            self.assertLess(start, pos)
            self.assertLess(pos, end)
        self.assertIn('#if defined(__NetBSD__)', adapter)
        self.assertIn('#ifdef _NETBSD_SOURCE\n#error', adapter)
        self.assertNotIn('HAVE_LISTING_', adapter)
        self.assertNotIn('extern ', adapter)
        defs = (COMPAT / 'compat_defs.h').read_text()
        self.assertIn('#error _NETBSD_SOURCE is *not* to be defined.', defs)
        self.assertNotIn('compat_acl_features.h', defs)
        self.assertIn('config.cache: compat_acl_features.h', (COMPAT / 'Makefile').read_text())
        self.assertIn('#undef EMBERBSD_COMPAT_ACL_NATIVE_HEADERS\n'
                      '/* Finite Darwin builds preinclude their configuration before this source. */\n'
                      '#if HAVE_LISTING_ACL_BSD\n#include "compat_acl_features.h"\n#endif', source)
        for path in value['sources']:
            if path != 'tools/compat/compat_acl.c':
                self.assertNotIn('EMBERBSD_COMPAT_ACL_NATIVE_HEADERS', (ROOT / path).read_text())

    def test_netbsd_extension_preload_preserves_normal_fallback_definitions(self):
        # These retained source headers explain why restricted preload is required.
        endian = (ROOT / 'sys/sys/endian.h').read_text()
        self.assertIn('#ifdef _NETBSD_SOURCE', endian)
        self.assertIn('__GEN_ENDIAN_ENC(16, be)', endian)
        types = (ROOT / 'sys/sys/types.h').read_text()
        self.assertIn('#include <machine/endian.h>', types)
        feature = (ROOT / 'sys/sys/featuretest.h').read_text()
        self.assertIn('!defined(_XOPEN_SOURCE) && !defined(_NETBSD_SOURCE)', feature)
        defs = (COMPAT / 'compat_defs.h').read_text()
        self.assertIn('#if !HAVE_DECL_BE16ENC\n__GEN_ENDIAN_ENC(16, be)', defs)
        native = (ROOT / 'sys/sys/stat.h').read_text()
        pattern = r'^#define\s+((?:UF|SF)_[A-Z]+)\s+(0x[0-9a-fA-F]+)'
        native_flags = dict(re.findall(pattern, native, re.M))
        for name, value in re.findall(pattern, defs, re.M):
            self.assertEqual(value, native_flags[name], name)
        for path, identifier in (('include/unistd.h', ' lpathconf('),
                                 ('sys/sys/unistd.h', '_PC_ACL_NFS4'),
                                 ('sys/sys/stat.h', 'S_ISWHT(')):
            stack = []
            found = False
            for line in (ROOT / path).read_text().splitlines():
                if re.match(r'^#\s*(if|ifdef|ifndef)\b', line):
                    stack.append(line)
                elif re.match(r'^#\s*endif\b', line):
                    stack.pop()
                if identifier in line:
                    self.assertTrue(any('_NETBSD_SOURCE' in guard for guard in stack), path)
                    found = True
                    break
            self.assertTrue(found, path)

    def test_normal_and_finite_share_full_source_fragment(self):
        fragment = COMPAT / 'listing.mk'
        self.assertTrue(fragment.is_file(), 'the common listing source fragment is missing')
        value = json.loads((COMPAT / 'bootstrap-listing.json').read_text())
        self.assertEqual(value['sources'], ['lib/libc/gen/getbsize.c',
            'lib/libc/gen/humanize_number.c', 'lib/libutil/stat_flags.c',
            'tools/compat/compat_acl.c'])
        for name in value['sources']:
            self.assertIn(Path(name).name, fragment.read_text())
        make = (COMPAT / 'Makefile').read_text()
        self.assertIn('COMPAT_LISTING_SRCS', make)
        self.assertNotIn('stat_flags.c strlcat.c', make)

    def test_canonical_formatter_bodies_and_target_integration_are_preserved(self):
        expected = {'lib/libc/gen/getbsize.c': '6d8a2254ead1f299477919ae78adc658f85d2f69c846667c4a2177671262279f', 'lib/libc/gen/humanize_number.c': '2f311138d6388adbd8ce03e1d0d2d197712f287dd41d15b7ed3e7ec3c09da77e', 'lib/libutil/stat_flags.c': '6d6995fcdba1131cfc227458ac31af712d1d3e950e924e0896b2c8f72b2012cc'}
        for name, digest in expected.items():
            raw = (ROOT / name).read_bytes()
            if name.endswith('stat_flags.c'):
                raw = raw.replace(b'#if HAVE_NBTOOL_CONFIG_H\n#include "compat_listing.h"\n#else\n#include "util.h"\n#endif', b'#include "util.h"', 1)
            self.assertEqual(hashlib.sha256(raw).hexdigest(), digest, name)

    def test_host_formatting_constants_match_the_canonical_abi(self):
        native = (ROOT / 'include/stdlib.h').read_text()
        shared = (COMPAT / 'compat_listing.h').read_text()
        pattern = r'^#define\s+(HN_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+)'
        self.assertEqual(dict(re.findall(pattern, shared, re.M)),
                         dict(re.findall(pattern, native, re.M)))
        for name in ('getbsize', 'humanize_number', 'flags_to_string', 'string_to_flags'):
            self.assertIn('#define ' + name + ' __nbcompat_' + name, shared)
        stat = (ROOT / 'lib/libutil/stat_flags.c').read_text()
        self.assertIn('#if HAVE_NBTOOL_CONFIG_H\n#include "compat_listing.h"\n#else\n#include "util.h"\n#endif', stat)

    def test_normal_and_finite_observe_the_same_native_capabilities(self):
        value = json.loads((COMPAT / 'bootstrap-listing.json').read_text())
        ac = (COMPAT / 'configure.ac').read_text()
        generated = (COMPAT / 'configure').read_text()
        template = (COMPAT / 'nbtool_config.h.in').read_text()
        for probe in value['probes']:
            include = '#include "$srcdir/probes/' + probe['name'] + '.c"'
            self.assertEqual(ac.count(include), 1)
            self.assertEqual(generated.count(include), 1)
            self.assertIn('AC_DEFINE([' + probe['macro'] + '], [1]', ac)
            self.assertIn('#define ' + probe['macro'] + ' 1', generated)
            self.assertIn('#undef ' + probe['macro'], template)
            self.assertNotIn('#define ' + probe['macro'], value['config_preamble'] + value['config_postamble'])
            self.assertNotIn('extern ', (ROOT / probe['source']).read_text())
        self.assertEqual(value['required_capabilities'], ['HAVE_LISTING_STAT_FLAGS', 'HAVE_LISTING_ACL_DARWIN'])
        self.assertEqual(value['required_tool_roles'], ['cc', 'ar', 'ranlib'])
        paths = [value['source_makefile'], value['source_fragment'], *value['sources'],
                 *value['headers'], *value['provenance'], value['control'], value['check'],
                 *(probe['source'] for probe in value['probes'])]
        self.assertEqual(len(paths), len(set(paths)))
        for name in paths:
            self.assertTrue((ROOT / name).is_file(), name)
            self.assertFalse((ROOT / name).is_symlink(), name)

    def test_public_listing_headers_are_installed_but_never_cleaned_as_generated_files(self):
        make = (COMPAT / 'Makefile').read_text()
        installed = re.search(r'^INCFILES=\s*(.*)$', make, re.M).group(1).split()
        self.assertIn('compat_listing.h', installed)
        self.assertIn('compat_acl.h', installed)
        clean = '\n'.join(line for line in make.splitlines() if line.startswith(('CLEANFILES', 'CLEANDIRFILES')))
        self.assertNotIn('${INCFILES}', clean)
        self.assertNotIn('compat_listing.h', clean)
        self.assertNotIn('compat_acl.h', clean)

    def test_acl_backend_is_shared_and_does_not_turn_missing_paths_into_absence(self):
        source = (COMPAT / 'compat_acl.c').read_text()
        self.assertIn('lstatx_np(path, &st, security)', source)
        self.assertIn('filesec_query_property(security, FILESEC_ACL, &valid)', source)
        self.assertIn('if (!valid)', source)
        self.assertNotIn('errno == ENOENT', source)
        self.assertIn('lpathconf(path, _PC_ACL_NFS4)', source)
        self.assertIn('lpathconf(path, _PC_ACL_EXTENDED)', source)
        self.assertIn('acl_is_trivial_np(acl, &trivial)', source)
        unsupported = source.split('#else\nstatic int\npresence', 1)[1].split('#endif', 1)[0]
        self.assertIn('errno = ENOTSUP;', unsupported)
        self.assertIn('return (-1);', unsupported)
        self.assertNotIn('*present = 0', unsupported)
        check = (COMPAT / 'probes/listing-check.c').read_text()
        self.assertIn('acl_get_fd_np(fd, ACL_TYPE_EXTENDED)', check)
        self.assertIn('errno != ENOENT || present != 7', check)
        self.assertIn('before.st_ino != after.st_ino', check)
        self.assertIn('link_created && unlink("listing-link")', check)
        self.assertIn('file_created && unlink("listing-file")', check)

if __name__ == '__main__':
    unittest.main()
