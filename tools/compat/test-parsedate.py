#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted). Source contract checks; no native execution.
"""Check parser preservation and common normal/finite build inputs."""

import hashlib
import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPAT = ROOT / "tools/compat"


class ParsedateSourceTests(unittest.TestCase):
    def test_canonical_parser_is_preserved(self):
        text = (ROOT / "lib/libutil/parsedate.y").read_text()
        self.assertIn('#include "compat_parsedate_features.h"', text)
        text = text.replace(
            '#if HAVE_NBTOOL_CONFIG_H\n'
            '#include "compat_parsedate_features.h"\n'
            '#include "nbtool_config.h"\n#endif\n\n', '', 1)
        text = text.replace(
            '#if HAVE_NBTOOL_CONFIG_H\n#include <stdint.h>\n'
            '#include "compat_parsedate.h"\n#else\n#include <util.h>\n#endif',
            '#include <util.h>', 1)
        selection = (
            '#if !HAVE_NBTOOL_CONFIG_H || HAVE_MKTIME_Z\n'
            '\t    result = mktime_z(NULL, &tm);\n'
            '#elif HAVE_TIMEGM\n\t    result = timegm(&tm);\n'
            '#else\n#error "parsedate needs a probed UTC conversion backend"\n'
            '#endif')
        self.assertIn(selection, text)
        text = text.replace(selection, '\t    result = mktime_z(NULL, &tm);', 1)
        self.assertEqual(hashlib.sha256(text.encode()).hexdigest(),
            'ec194cffcb527a4a56cbe1ab5f158239f88d5f96e643390eb2193667a62ef2e4')

    def test_manifest_has_complete_source_and_generator(self):
        data = json.loads((COMPAT / 'bootstrap-parsedate.json').read_text())
        self.assertEqual(data['sources'], ['lib/libutil/parsedate.y'])
        self.assertEqual(data['required_tool_roles'], ['cc', 'yacc', 'ar', 'ranlib'])
        self.assertEqual(data['generator'], {
            'role': 'yacc', 'arguments': ['-p', '__pd', '-o', 'parsedate.c'],
            'source': 'lib/libutil/parsedate.y', 'output': 'parsedate.c'})
        self.assertEqual(data['required_capabilities'], ['HAVE_TIMEGM'])
        self.assertEqual([p['macro'] for p in data['probes']],
                         ['HAVE_MKTIME_Z', 'HAVE_TIMEGM'])
        self.assertNotRegex(data['config_preamble'] + data['config_postamble'],
                            r'#define HAVE_(MKTIME_Z|TIMEGM)')
        roster = [data['source_makefile'], data['source_fragment'],
                  *data['sources'], *data['headers'], *data['provenance'],
                  data['control'], data['check'],
                  *(p['source'] for p in data['probes'])]
        self.assertEqual(len(roster), len(set(roster)))
        for name in roster:
            with self.subTest(name=name):
                self.assertTrue((ROOT / name).is_file())
                self.assertFalse((ROOT / name).is_symlink())

    def test_normal_build_uses_selected_generator(self):
        make = (COMPAT / 'Makefile').read_text()
        self.assertLess(make.index('${SRCS:M*.c}'), make.index('/parsedate.mk'))
        self.assertIn('${COMPAT_PARSEDATE_SRCS}', make)
        self.assertIn('!defined(HOST_YACC)', make)
        self.assertIn('empty(HOST_YACC:M/*)', make)
        self.assertIn('${HOST_YACC:[#]} != 1', make)
        self.assertIn('${HOST_YACC:Q} ${COMPAT_PARSEDATE_YFLAGS}', make)
        self.assertNotIn('HOST_YACC?=', make)
        fragment = (COMPAT / 'parsedate.mk').read_text()
        self.assertRegex(fragment, r'COMPAT_PARSEDATE_SRCS=\s+parsedate.c')
        self.assertRegex(fragment, r'COMPAT_PARSEDATE_YFLAGS=\s+-p __pd')
        self.assertIn('yacc yacc HOST_YACC\n',
                      (ROOT / 'tools/native-tools/contract').read_text())
        build = (ROOT / 'build.sh').read_text()
        self.assertIn('HOST_YACC) HOST_YACC=${_ember_native_target}', build)
        self.assertIn('HOST_LD HOST_RANLIB HOST_LN HOST_YACC', build)
        self.assertIn('HOST_RANLIB|HOST_LN|HOST_YACC|', build)

    def test_normal_and_finite_share_actual_probes(self):
        ac = (COMPAT / 'configure.ac').read_text()
        generated = (COMPAT / 'configure').read_text()
        header = (COMPAT / 'nbtool_config.h.in').read_text()
        data = json.loads((COMPAT / 'bootstrap-parsedate.json').read_text())
        for probe in data['probes']:
            name, macro = probe['name'], probe['macro']
            include = '#include "$srcdir/probes/' + name + '.c"'
            with self.subTest(name=name):
                self.assertIn(include, ac)
                self.assertIn(include, generated)
                self.assertIn('AC_DEFINE([' + macro + '], [1]', ac)
                self.assertIn('#define ' + macro + ' 1', generated)
                self.assertIn('#undef ' + macro, header)
                source = (ROOT / probe['source']).read_text()
                self.assertIn('#include <time.h>', source)
                self.assertIn('#include "compat_parsedate_features.h"', source)
                self.assertNotIn('extern ', source)
        self.assertIn('ac_fn_c_try_link', generated[generated.index(
            'for mktime_z with its time.h declaration'):])

    def test_declarations_and_feature_selection_do_not_shadow_time(self):
        header = (COMPAT / 'compat_parsedate.h').read_text()
        self.assertIn('time_t parsedate(const char *, const time_t *, const int *);', header)
        features = (COMPAT / 'compat_parsedate_features.h').read_text()
        self.assertNotRegex(features, r'\bHAVE_\w+')
        self.assertNotIn('typedef', features)
        self.assertIn('#include "compat_parsedate.h"',
                      (COMPAT / 'compat_defs.h').read_text())
        data = json.loads((COMPAT / 'bootstrap-parsedate.json').read_text())
        self.assertTrue(all(Path(name).name not in ('time.h', 'util.h')
                            for name in data['headers']))


if __name__ == '__main__':
    unittest.main()
