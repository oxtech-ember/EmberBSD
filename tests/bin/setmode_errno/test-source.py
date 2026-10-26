#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Check setmode callers and emit a diagnostic fixture. Never execute C."""
import argparse
import hashlib
from pathlib import Path
import re
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[3]
ORIGINALS = {
    'chmod': '68e322570ae062b5824090d8d673cd553646097ae2f6e8949addb7d3a017330f',
    'mkdir': '96483831f23a056755f778bba2b41b296376ea0277575702422dd2f3f957714c'}


def fragment(name):
    text = (ROOT / 'bin' / name / (name + '.c')).read_text()
    marker = 'if ((set = setmode('
    start = text.index(marker)
    start = text.rfind('\n', 0, start) + 1
    previous = text.rfind('\n', 0, start - 1) + 1
    if text[previous:start].strip() == 'errno = 0;':
        start = previous
    brace = text.index('{', start)
    level = 1
    end = brace + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text, text[start:end]


def original_fragment(name):
    arg = 'mode' if name == 'chmod' else 'optarg'
    indent = '\t\t' if name == 'chmod' else '\t\t\t'
    message = ('err(EXIT_FAILURE, "Cannot set file mode `%s\'", mode);' if name == 'chmod' else
               'err(EXIT_FAILURE, "Cannot set file mode `%s\'",\n' + indent + '\t    optarg);')
    return (indent + 'if ((set = setmode(' + arg + ')) == NULL) {\n' +
            indent + '\t' + message + '\n' + indent + '\t/* NOTREACHED */\n' + indent + '}')


CASES = [
    ('null-zero', '0', 'NO_ERROR', 'XERR', '0'),
    ('null-stale-io', 'EIO', 'NO_ERROR', 'XERR', '0'),
    ('null-stale-memory', 'ENOMEM', 'NO_ERROR', 'XERR', '0'),
    ('fresh-invalid', 'EIO', 'INVALID', 'ERR', 'EINVAL'),
    ('allocator-failure', 'EIO', 'ALLOCATE', 'ERR', 'ENOMEM'),
    ('success-stale', 'EIO', 'SUCCESS', 'NONE', '0')]

FIXTURE = r'''/* SPDX-License-Identifier: BSD-2-Clause
 * Origin: EmberBSD contributors (AI-assisted).
 * Diagnostic caller fragments only. Never a production setmode replacement.
 */
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum outcome { NONE, ERR, XERR };
enum behavior { NO_ERROR, INVALID, ALLOCATE, SUCCESS };
static jmp_buf jump;
static enum behavior selected;
static enum outcome seen;
static int saved_error;
static int saved_status;
static char message[128];
static char token;

static void *
probe_setmode(const char *mode)
{

	(void)mode;
	if (selected == SUCCESS)
		return &token;
	if (selected == INVALID)
		errno = EINVAL;
	if (selected == ALLOCATE) {
		volatile size_t count = SIZE_MAX;
		void *memory;

		/* Overflow fails allocation without consuming host memory. */
		memory = calloc(count, 2);
		if (memory != NULL) {
			free(memory);
			abort();
		}
		if (errno != ENOMEM)
			abort();
	}
	return NULL;
}

static void
probe_err(int status, const char *format, ...)
{
	va_list ap;

	saved_error = errno;
	saved_status = status;
	seen = ERR;
	va_start(ap, format);
	(void)vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	longjmp(jump, 1);
}

static void
probe_errx(int status, const char *format, ...)
{
	va_list ap;

	saved_error = errno;
	saved_status = status;
	seen = XERR;
	va_start(ap, format);
	(void)vsnprintf(message, sizeof(message), format, ap);
	va_end(ap);
	longjmp(jump, 1);
}

static void *
chmod_caller(const char *mode)
{
	void *set;

@CHMOD@
	return set;
}

static void *
mkdir_caller(const char *optarg)
{
	void *set;

@MKDIR@
	return set;
}

struct testcase {
	const char *name;
	int before;
	enum behavior behavior;
	enum outcome expected;
	int error;
};
static const struct testcase cases[] = {
@CASES@
};

int
main(void)
{
	size_t caller, index;
	void *answer;

	for (caller = 0; caller < 2; caller++) {
		for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
			selected = cases[index].behavior;
			seen = NONE;
			saved_error = saved_status = 0;
			message[0] = '\0';
			errno = cases[index].before;
			if (setjmp(jump) == 0) {
				answer = caller == 0 ? chmod_caller("invalid") :
				    mkdir_caller("invalid");
				if (answer != &token || cases[index].expected != NONE)
					return 1;
			}
			if (seen != cases[index].expected ||
			    saved_error != cases[index].error ||
			    (seen != NONE && (saved_status != EXIT_FAILURE ||
			    strcmp(message, "Cannot set file mode `invalid'") != 0))) {
				(void)fprintf(stderr, "%zu:%s failed\n", caller, cases[index].name);
				return 1;
			}
		}
	}
	return 0;
}
'''


def fixture_source():
    result = FIXTURE
    for name in ORIGINALS:
        block = textwrap.dedent(fragment(name)[1])
        block = re.sub(r'\bsetmode\(', 'probe_setmode(', block)
        block = re.sub(r'\berrx\(', 'probe_errx(', block)
        block = re.sub(r'\berr\(', 'probe_err(', block)
        result = result.replace('@' + name.upper() + '@', textwrap.indent(block, '\t'))
    cases = ['\t{ "' + name + '", ' + ', '.join(values) + ' },' for name, *values in CASES]
    return result.replace('@CASES@', '\n'.join(cases))


class SourceTests(unittest.TestCase):
    def test_errno_is_cleared_immediately_before_each_call(self):
        for name in ORIGINALS:
            with self.subTest(caller=name):
                block = textwrap.dedent(fragment(name)[1])
                self.assertRegex(block, r'^errno = 0;\nif \(\(set = setmode\(')
                self.assertEqual(block.count('errno = 0;'), 1)

    def test_fresh_errno_is_kept_and_zero_errno_uses_errx(self):
        for name in ORIGINALS:
            with self.subTest(caller=name):
                block = fragment(name)[1]
                self.assertRegex(block, r'if \(errno != 0\)\s*err\(EXIT_FAILURE,')
                self.assertIn('errx(EXIT_FAILURE,', block)
                self.assertLess(block.index('err(EXIT_FAILURE,'), block.index('errx(EXIT_FAILURE,'))
                self.assertNotRegex(block.split('setmode(', 1)[1], r'errno\s*=')

    def test_all_other_caller_source_bytes_are_unchanged(self):
        for name, expected in ORIGINALS.items():
            with self.subTest(caller=name):
                text, block = fragment(name)
                restored = text.replace(block, original_fragment(name), 1)
                self.assertEqual(hashlib.sha256(restored.encode()).hexdigest(), expected)

    def test_diagnostic_uses_extracted_callers_and_real_allocation_failure(self):
        generated = fixture_source()
        self.assertNotIn('@CHMOD@', generated)
        self.assertNotIn('@MKDIR@', generated)
        self.assertEqual(generated.count('probe_setmode('), 3)
        self.assertIn('memory = calloc(count, 2);', generated)
        self.assertNotIn('errno = ENOMEM;', generated)
        self.assertNotIn(chr(92) + 't', generated)
        self.assertNotIn("'" + chr(92) * 2 + "0'", generated)
        self.assertNotIn(chr(0), generated)
        self.assertIn('failed' + chr(92) + 'n"', generated)
        self.assertEqual(len(CASES), 6)


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--emit-fixture', type=Path)
    args, remaining = cli.parse_known_args()
    if args.emit_fixture is not None:
        if not args.emit_fixture.is_absolute():
            cli.error('The new diagnostic path must be absolute.')
        with args.emit_fixture.open('x') as stream:
            stream.write(fixture_source())
    unittest.main(argv=[__file__, *remaining])
