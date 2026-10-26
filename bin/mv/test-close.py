#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Emit a diagnostic for the actual fastcopy close blocks, not a native mv."""
import argparse
from pathlib import Path


def blocks(source):
    body = source.split('\nfastcopy(', 1)[1].split('\nstatic int\ncopy(', 1)[0]
    source_close = body.split('\t\twarn("%s: error copying extended attributes", to);\n', 1)[1]
    source_close = source_close.split('#ifdef BSD4_4', 1)[0]
    start = body.index('\tif (close(to_fd))')
    end = body.index('\n\t}', start) + len('\n\t}')
    return source_close, body[start:end]


def diagnostic(source):
    first, second = blocks(source)
    return r'''/* Diagnostic only: exact close blocks from canonical mv.c. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int source_fd, source_error, destination_error;
static int source_calls, destination_calls, warnings, removed;

static int
test_close(int fd)
{
	if (fd == source_fd) {
		source_calls++;
		errno = source_error;
		return source_error ? -1 : 0;
	}
	assert(fd == 11);
	destination_calls++;
	errno = destination_error;
	return destination_error ? -1 : 0;
}

static void
test_warn(const char *format, const char *path)
{
	assert(strcmp(format, "%s") == 0);
	assert(strcmp(path, "source") == 0 || strcmp(path, "destination") == 0);
	warnings++;
}

#define close test_close
#define warn test_warn
static int
exercise(void)
{
	int from_fd = source_fd, to_fd = 11;
	const char *from = "source", *to = "destination";
	/* The historical ignored-close control does not use from. */
	(void)from;
''' + first + '\n' + second + r'''
	removed++;
	return 0;
}
#undef close
#undef warn

int
main(void)
{
	const int descriptors[] = { 0, 7 };
	const int source_errors[] = { 0, EIO, EINTR };
	const int destination_errors[] = { 0, EIO };
	unsigned int i, j, k, cases = 0;
	int result, failed;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 3; j++) {
			for (k = 0; k < 2; k++) {
				source_fd = descriptors[i];
				source_error = source_errors[j];
				destination_error = destination_errors[k];
				source_calls = destination_calls = warnings = removed = 0;
				result = exercise();
				failed = source_error != 0 || destination_error != 0;
				if (result != failed || removed != !failed ||
					source_calls != 1 || destination_calls != 1 ||
					warnings != failed) {
					fprintf(stderr, "close case failed: fd=%d source=%d destination=%d\n",
						source_fd, source_error, destination_error);
					return 1;
				}
				cases++;
			}
		}
	}
	printf("%u close cases passed\n", cases);
	return 0;
}
'''


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).with_name('mv.c'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with args.output.open('x') as stream:
        stream.write(diagnostic(args.source.read_text()))
