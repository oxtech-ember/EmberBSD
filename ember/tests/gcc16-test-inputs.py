#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: AI-assisted EmberBSD regression of test input boundaries.
"""Check binary test inputs and regex continuation with the actual parser."""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def declaration(path, pattern):
    matches = re.findall(pattern, path.read_text(), re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError(f"expected one declaration in {path}")
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.source.resolve()
    cc = shlex.split(os.environ.get("HOST_CC", "gcc"))
    flags = shlex.split(os.environ.get("HOST_CFLAGS", ""))
    fixtures = root / "tests/lib/libc"
    inp = declaration(fixtures / "locale/t_iconv.c", r"^\s*(char in\[\d+\] = .*?;)")
    delim = declaration(fixtures / "regex/t_regex_att.c", r"^(static const char delim\[\d+\] = .*?;)")
    temp = tempfile.TemporaryDirectory(prefix="gcc16-test-inputs-") if args.output is None else None
    output = Path(temp.name) if temp else args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    try:
        # Use the complete production parser. Only host header and line-input
        # adapters differ from its native libc build.
        (output / "namespace.h").write_text("")
        (output / "compat.h").write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#define HAVE_NBTOOL_CONFIG_H 1
#define _DIAGASSERT assert
#ifndef FPARSELN_UNESCALL
#define FPARSELN_UNESCESC 1
#define FPARSELN_UNESCCONT 2
#define FPARSELN_UNESCCOMM 4
#define FPARSELN_UNESCREST 8
#define FPARSELN_UNESCALL 15
#endif
char *probe_fgetln(FILE *, size_t *);
#define fgetln probe_fgetln
#define fparseln probe_fparseln
''')
        (output / "probe.c").write_text(r'''
#include <string.h>
static char *line_buffer;
static size_t line_capacity;
char *probe_fgetln(FILE *fp, size_t *length)
{
    ssize_t count = getline(&line_buffer, &line_capacity, fp);
    if (count < 0)
        return NULL;
    *length = (size_t)count;
    return line_buffer;
}
char *probe_fparseln(FILE *, size_t *, size_t *, const char [3], int);
''' + delim + r'''
static void check_line(const char *input, const char *expected, size_t lines)
{
    FILE *fp = tmpfile();
    size_t length = 0, number = 0;
    char *result;
    assert(fp != NULL);
    int status = fputs(input, fp);
    assert(status >= 0);
    rewind(fp);
    result = probe_fparseln(fp, &length, &number, delim, 0);
    assert(result != NULL);
    assert(length == strlen(expected));
    assert(strcmp(result, expected) == 0);
    assert(number == lines);
    free(result);
    status = fclose(fp);
    assert(status == 0);
}
int main(int argc, char **argv)
{
''' + inp + r'''
    const unsigned char expected[] = { 0x7e, 0x7b, 0x7e, 0x7e };
    assert(argc == 2);
    if (strcmp(argv[1], "iconv") == 0) {
        assert(sizeof(in) == sizeof(expected));
        assert(memcmp(in, expected, sizeof(expected)) == 0);
    } else {
        check_line("ab\\\ncd\n", "abcd", 2);
        check_line("ab\\\\\ncd\n", "ab\\\\", 1);
        check_line("ab#cd\n", "ab#cd", 1);
    }
    free(line_buffer);
    return 0;
}
''')
        binary = output / "probe"
        command = cc + flags + ["-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                               "-I", str(output), "-include", str(output / "compat.h"),
                               str(root / "lib/libc/stdio/fparseln.c"), str(output / "probe.c"),
                               "-o", str(binary)]
        subprocess.run(command, check=True)
        failed = False
        for case in ("iconv", "regex"):
            run = subprocess.run([str(binary), case], check=False)
            print(f"{case}: {'PASS' if run.returncode == 0 else 'FAIL'}", flush=True)
            failed |= run.returncode != 0
        return int(failed)
    finally:
        if temp:
            temp.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
