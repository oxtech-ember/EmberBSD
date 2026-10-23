#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Exercise the actual early FDT trace and libfdt.
"""Check bounded FDT bootstrap diagnostics with the owning libfdt sources."""
import argparse
import os
from pathlib import Path
import shlex
import tempfile

from fdt_contract import build_and_run


def emit(source, work):
    text = (source / "sys/arch/evbarm/fdt/fdt_machdep.c").read_text()
    start = text.index("#ifdef VERBOSE_INIT_ARM\n#define VPRINTF")
    end = text.index("\nstatic void\nfdt_add_dram_blocks", start)
    block = text[start:end]
    fixture = r'''
#include <stdio.h>
#include <stdarg.h>
#include <libfdt.h>
static unsigned checks;
#define CHECK(v) do { checks++; if (!(v)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #v); exit(1); } } while (0)
static char output[8192];
static size_t used;
static int
trace_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(output + used, sizeof(output) - used, format, args);
    va_end(args);
    CHECK(n >= 0 && (size_t)n < sizeof(output) - used);
    used += (size_t)n;
    return n;
}
#define printf trace_printf
#define VERBOSE_INIT_ARM
BLOCK
#undef VERBOSE_INIT_ARM
#undef VPRINTF
#undef VPRINT_FDT
#undef printf
#define __nothing ((void)0)
BLOCK

static unsigned char tree[524288], saved[sizeof(tree)], copy[sizeof(tree)];
static void
trace(const char *stage, const void *data, size_t bound)
{
    used = 0;
    output[0] = '\0';
    if (data != NULL)
        memcpy(saved, data, sizeof(saved));
    fdt_bootstrap_trace(stage, data, bound);
    if (data != NULL)
        CHECK(memcmp(saved, data, sizeof(saved)) == 0);
}
static void
contains(const char *expected)
{
    CHECK(strstr(output, expected) != NULL);
}
static void
property(const char *node, const char *name, const void *value, int len)
{
    int off = fdt_path_offset(tree, node);
    CHECK(off >= 0);
    CHECK(fdt_setprop(tree, off, name, value, len) == 0);
    CHECK(fdt_check_full(tree, sizeof(tree)) == 0);
}
static void
make_tree(void)
{
    memset(tree, 0, sizeof(tree));
    CHECK(fdt_create_empty_tree(tree, sizeof(tree)) == 0);
    CHECK(fdt_add_subnode(tree, 0, "cpus") >= 0);
    CHECK(fdt_add_subnode(tree, 0, "uart@1000") >= 0);
    CHECK(fdt_add_subnode(tree, 0, "aliases") >= 0);
    CHECK(fdt_add_subnode(tree, 0, "chosen") >= 0);
    property("/aliases", "serial0", "/uart@1000", sizeof("/uart@1000"));
    property("/chosen", "stdout-path", "serial0:115200n8", sizeof("serial0:115200n8"));
}
int
main(int argc, char **argv)
{
    /* The disabled macro must not evaluate any argument. */
    unsigned side_effect = 0;
    VPRINT_FDT(++side_effect, ++side_effect, ++side_effect);
    CHECK(side_effect == 0);
    trace("small", NULL, 0);
    contains("header=-8");
    CHECK(strstr(output, "total=") == NULL);
    make_tree();
    trace("valid", tree, sizeof(tree));
    contains("header=0"); contains("full=0"); contains("root=0 cpus=");
    CHECK(strstr(output, "serial0=-") == NULL);
    contains("stdout-path=serial0:115200n8");
    trace("limited", tree, 128);
    contains("full=skipped bound=128");
    CHECK(strstr(output, "cpus=") == NULL);
    fdt_set_magic(tree, 0);
    trace("header", tree, sizeof(tree)); contains("header=-9");
    CHECK(strstr(output, "total=") == NULL);
    make_tree();
    fdt32_t bad = cpu_to_fdt32(0xdeadbeef);
    memcpy(tree + fdt_off_dt_struct(tree), &bad, sizeof(bad));
    CHECK(fdt_check_header(tree) == 0);
    CHECK(fdt_open_into(tree, copy, sizeof(copy)) == 0);
    CHECK(fdt_check_full(copy, sizeof(copy)) == -FDT_ERR_BADSTRUCTURE);
    trace("malformed", tree, sizeof(tree));
    contains("full=-11"); contains("first-tag=0xdeadbeef paths=skipped");
    CHECK(strstr(output, "cpus=") == NULL);
    make_tree();
    const char *bad_aliases[] = {"serial0", "", "relative", "xxx"};
    for (unsigned n = 0; n < sizeof(bad_aliases) / sizeof(bad_aliases[0]); n++) {
        property("/aliases", "serial0", bad_aliases[n], (int)strlen(bad_aliases[n]) + 1);
        trace("alias", tree, sizeof(tree)); contains("serial0=-15");
    }
    property("/aliases", "serial0", "/xyz", 4); /* No NUL. */
    trace("alias", tree, sizeof(tree)); contains("serial0=-15");
    property("/aliases", "serial0", "/\0xyz", 6); /* Embedded NUL. */
    trace("alias", tree, sizeof(tree)); contains("serial0=-15");
    property("/aliases", "serial0", "", 0);
    trace("alias", tree, sizeof(tree)); contains("serial0=-15");
    char large[129]; memset(large, 'a', sizeof(large));
    large[0] = '/'; large[sizeof(large) - 1] = '\0';
    property("/aliases", "serial0", large, sizeof(large));
    trace("alias", tree, sizeof(tree)); contains("serial0=-15");
    const char *bad_stdout[] = {"abc", "a\0b", ""};
    const int bad_lengths[] = {3, 4, 0};
    for (unsigned n = 0; n < 3; n++) {
        property("/chosen", "stdout-path", bad_stdout[n], bad_lengths[n]);
        trace("stdout", tree, sizeof(tree)); contains("stdout-path=invalid");
    }
    property("/chosen", "stdout-path", large, sizeof(large));
    trace("stdout", tree, sizeof(tree)); contains("stdout-path=invalid len=129");
    large[127] = '\0';
    property("/chosen", "stdout-path", large, 128);
    trace("stdout", tree, sizeof(tree));
    CHECK(strstr(output, "stdout-path=invalid") == NULL);
    CHECK(fdt_delprop(tree, fdt_path_offset(tree, "/chosen"), "stdout-path") == 0);
    trace("missing", tree, sizeof(tree)); contains("stdout-path=-1");
    CHECK(fdt_del_node(tree, fdt_path_offset(tree, "/chosen")) == 0);
    CHECK(fdt_del_node(tree, fdt_path_offset(tree, "/aliases")) == 0);
    trace("missing", tree, sizeof(tree)); contains("aliases=-1 chosen=-1 serial0=-1");
    make_tree();
    CHECK(fdt_pack(tree) == 0);
    CHECK(fdt_open_into(tree, copy, sizeof(copy)) == 0);
    CHECK(fdt_check_full(copy, sizeof(copy)) == 0);
    CHECK(fdt_path_offset(tree, "/cpus") == fdt_path_offset(copy, "/cpus"));
    CHECK(fdt_pack(copy) == 0);
    CHECK(fdt_check_full(copy, sizeof(copy)) == 0);
    if (argc == 2) {
        FILE *input = fopen(argv[1], "rb"); CHECK(input != NULL);
        size_t size = fread(tree, 1, sizeof(tree), input);
        CHECK(!ferror(input) && feof(input)); CHECK(fclose(input) == 0);
        CHECK(size >= sizeof(struct fdt_header));
        CHECK(fdt_check_full(tree, size) == 0);
        trace("input", tree, size); fputs(output, stdout);
        CHECK(fdt_open_into(tree, copy, sizeof(copy)) == 0);
        trace("copied", copy, sizeof(copy)); fputs(output, stdout);
        CHECK(fdt_pack(copy) == 0);
        trace("packed", copy, sizeof(copy)); fputs(output, stdout);
    } else {
        CHECK(argc == 1);
    }
    printf("PASS: %u actual-source FDT trace checks\n", checks);
    return 0;
}
'''.replace("BLOCK", block)
    (work / "contract.c").write_text(fixture)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--emit", type=Path)
    parser.add_argument("--dtb", type=Path, help="Also inspect an existing compiled DTB")
    args = parser.parse_args()
    if args.emit:
        args.emit.mkdir(parents=True, exist_ok=True)
        emit(args.source_root, args.emit)
        return
    flags = shlex.split(os.environ.get("FDT_TRACE_CFLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="fdt-bootstrap-") as tmp:
        work = Path(tmp)
        emit(args.source_root, work)
        arguments = [str(args.dtb)] if args.dtb else []
        build_and_run(args.source_root, work, flags, arguments)


if __name__ == "__main__":
    main()
