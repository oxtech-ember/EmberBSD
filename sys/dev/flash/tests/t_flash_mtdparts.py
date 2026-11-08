#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the production MTD partition size and range parser."""

import argparse
import pathlib
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--out-dir", required=True)
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()

root = pathlib.Path(__file__).resolve().parents[4]
source = (root / "sys/dev/flash/flash_mtdparts.c").read_text()


def extract(name):
    match = re.search(rf"static (?:flash_size_t|int)\n{name}\([^{{]+\)\n\{{", source)
    if not match:
        raise SystemExit(f"missing production function: {name}")
    depth = 1
    end = match.end()
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[match.start():end]


fixture = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef int64_t flash_off_t;
typedef uint64_t flash_size_t;
enum { FLASH_PART_READONLY = 1 };
struct flash_partition {
    flash_off_t part_offset;
    flash_size_t part_size;
    int part_flags;
    const char *part_name;
};

""" + extract("flash_parse_size") + "\n" + extract("flash_parse_partdef") + r"""

static void
expect_parse(flash_size_t flash_size, char *definition, flash_off_t *offset,
    struct flash_partition *part, int expected)
{
    int error = flash_parse_partdef(flash_size, definition, offset, part);
    assert(error == expected);
}

int
main(void)
{
    struct flash_partition part;
    flash_off_t offset;
    char too_large[] = "6M@0";
    char remainder_outside[] = "-@3M";
    char boot[] = "6M@0(boot)ro";
    char root[] = "10M@6M(root)";
    char unnamed[] = "1M";
    char wide[] = "5G";
    char decimal_overflow[] = "18446744073709551616";
    char suffix_overflow[] = "18446744073709551615K";
    char offset_overflow[] = "1M@9223372036854775808";
    char endpoint_overflow[] = "1M@9223372036854775807";
    char malformed_offset[] = "1M@x";
    char implicit[] = "1M";

    offset = 0;
    expect_parse(2ULL << 20, too_large, &offset, &part, EINVAL);
    assert(offset == 0);

    offset = 0;
    expect_parse(2ULL << 20, remainder_outside, &offset, &part, EINVAL);
    assert(offset == 0);

    expect_parse(UINT64_MAX, decimal_overflow, &offset, &part, EINVAL);
    expect_parse(UINT64_MAX, suffix_overflow, &offset, &part, EINVAL);
    expect_parse(UINT64_MAX, offset_overflow, &offset, &part, EINVAL);
    expect_parse(UINT64_MAX, endpoint_overflow, &offset, &part, EINVAL);
    expect_parse(16ULL << 20, malformed_offset, &offset, &part, EINVAL);
    offset = -1;
    expect_parse(16ULL << 20, implicit, &offset, &part, EINVAL);
    assert(offset == -1);

    offset = 0;
    expect_parse(16ULL << 20, boot, &offset, &part, 0);
    assert(part.part_offset == 0 && part.part_size == (6ULL << 20));
    assert(part.part_name != NULL && strcmp(part.part_name, "boot") == 0);
    assert(part.part_flags == FLASH_PART_READONLY && offset == (6ULL << 20));
    expect_parse(16ULL << 20, root, &offset, &part, 0);
    assert(part.part_offset == (6ULL << 20) && part.part_size == (10ULL << 20));
    assert(part.part_name != NULL && strcmp(part.part_name, "root") == 0);
    assert(offset == (16ULL << 20));

    offset = 0;
    expect_parse(16ULL << 20, unnamed, &offset, &part, 0);
    assert(part.part_name == NULL);

    offset = 0;
    expect_parse(8ULL << 30, wide, &offset, &part, 0);
    assert(part.part_size == (5ULL << 30));
    return 0;
}
"""

out_dir = pathlib.Path(args.out_dir)
out_dir.mkdir(parents=True, exist_ok=True)
c_path = out_dir / "t_flash_mtdparts.c"
binary = out_dir / "t_flash_mtdparts"
c_path.write_text(fixture)
command = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror"]
if args.sanitize:
    command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
command += [str(c_path), "-o", str(binary)]
subprocess.run(command, check=True)
subprocess.run([str(binary)], check=True)
