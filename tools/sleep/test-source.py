#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Check sleep portability sources. Do not execute native code."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def platform_source(text, apple):
    """Select only the explicit Apple branches; retain unrelated conditions."""
    output = []
    active = True
    stack = []
    for line in text.splitlines():
        token = line.strip()
        if token.startswith(('#if ', '#ifdef ', '#ifndef ')):
            selected = apple if token == '#ifdef __APPLE__' else None
            stack.append((active, selected))
            if selected is not None:
                active = active and selected
        elif token == '#else':
            parent, selected = stack[-1]
            if selected is not None:
                active = parent and not selected
        elif token.startswith('#endif'):
            active, _ = stack.pop()
        elif active:
            output.append(line)
    if stack:
        raise ValueError('unbalanced source conditions')
    return '\n'.join(output)


class SleepSourceTests(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / 'bin/sleep/sleep.c').read_text()
        path = ROOT / 'bin/sleep/sleep_darwin.h'
        self.darwin = path.read_text() if path.exists() else ''

    def test_darwin_uses_available_public_apis(self):
        self.assertIn('#ifdef __APPLE__\n#include "sleep_darwin.h"', self.source)
        for token in ('clock_gettime(CLOCK_MONOTONIC,', 'nanosleep(&remaining, NULL)',
                      'newlocale(LC_NUMERIC_MASK, "C",', 'strtod_l('):
            self.assertIn(token, self.darwin)
        for token in ('clock_nanosleep(', 'TIMER_ABSTIME', 'LC_C_LOCALE',
                      'timespecadd', 'timespecsub'):
            self.assertNotIn(token, self.darwin)

    def test_selected_darwin_path_has_no_missing_api(self):
        selected = platform_source(self.source, True)
        for token in ('clock_nanosleep(', 'TIMER_ABSTIME', 'LC_C_LOCALE',
                      'timespecadd(', 'timespecsub('):
            self.assertNotIn(token, selected)
        self.assertIn('sleep_until(&endtime)', selected)
        self.assertNotIn('sleep_until(&endtime)', platform_source(self.source, False))

    def test_native_clock_and_locale_paths_are_retained(self):
        self.assertIn('clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,', self.source)
        self.assertIn('val = strtod_l(arg, &temp, LC_C_LOCALE);', self.source)
        self.assertIn('timespecadd(&now, &ntime, &endtime);', self.source)
        self.assertIn('timespecsub(&endtime, &now, &ntime);', self.source)

    def test_wait_recomputes_from_absolute_deadline(self):
        self.assertIn('sleep_difference(deadline, &now, &remaining);', self.darwin)
        self.assertIn('deadline->tv_sec == now.tv_sec', self.darwin)
        self.assertIn('deadline->tv_nsec <= now.tv_nsec', self.darwin)
        self.assertIn('return errno;', self.darwin)
        self.assertRegex(self.darwin, r'if \(nanosleep\(&remaining, NULL\) != 0\)\s*return errno;')
        self.assertRegex(self.source, r'e = sleep_until\(&endtime\);')

    def test_difference_borrows_one_second(self):
        self.assertIn('result->tv_sec = end->tv_sec - start->tv_sec;', self.darwin)
        self.assertIn('result->tv_nsec = end->tv_nsec - start->tv_nsec;', self.darwin)
        self.assertRegex(self.darwin, r'if \(result->tv_nsec < 0\) \{\s*result->tv_sec--;\s*result->tv_nsec \+= 1000000000;')

    def test_deadline_checks_before_addition(self):
        self.assertIn('maximum - (uintmax_t)now->tv_sec', self.darwin)
        self.assertIn('return EOVERFLOW;', self.darwin)
        self.assertLess(self.darwin.index('return EOVERFLOW;'),
                        self.darwin.index('result->tv_sec = now->tv_sec + interval->tv_sec;'))
        self.assertIn('result->tv_sec == (time_t)maximum', self.darwin)

    def test_locale_lifetime_is_checked(self):
        self.assertRegex(self.darwin, r'if \(locale == NULL\)\s*err\(EXIT_FAILURE, "newlocale"\);')
        self.assertRegex(self.darwin, r'if \(freelocale\(locale\) != 0\)\s*err\(EXIT_FAILURE, "freelocale"\);')
        self.assertIn('errno = saved_errno;', self.darwin)

    def test_signal_error_convention_is_unchanged(self):
        self.assertIn('if (!report_requested || e != EINTR)', self.source)
        self.assertRegex(self.source, r'errno = e;\s*err\(EXIT_FAILURE,')
        self.assertIn('signal(SIGINFO, report_request);', self.source)
        self.assertIn('(void)signal(SIGALRM, alarmhandle);', self.source)
        self.assertIn('_exit(EXIT_SUCCESS);', self.source)

    def test_normal_host_recipe_keeps_compatibility_and_libm(self):
        path = ROOT / 'tools/sleep/Makefile'
        self.assertTrue(path.is_file())
        recipe = path.read_text()
        self.assertIn('HOST_SRCDIR=\tbin/sleep', recipe)
        self.assertIn('../Makefile.host', recipe)
        self.assertNotIn('NOCOMPATLIB', recipe)
        self.assertIn('LDADD+= -lm', (ROOT / 'bin/sleep/Makefile').read_text())
        self.assertIn('#if HAVE_NBTOOL_CONFIG_H', self.source)


if __name__ == '__main__':
    unittest.main()
