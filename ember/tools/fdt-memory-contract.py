#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD, AI-assisted. Check the actual memory iterator and libfdt.
"""Check termination and error reporting in the FDT memory iterator."""
import argparse
import os
from pathlib import Path
import shlex
import tempfile
from fdt_contract import build_and_run


def function(text, signature):
    start = text.index(signature)
    body = text.index("\n{", start) + 2
    end, depth = body, 1
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[start:end]


def emit(source, work):
    memory = function((source / "sys/dev/fdt/fdt_memory.c").read_text(),
                      "void\nfdt_memory_get(")
    subr = (source / "sys/dev/fdt/fdt_subr.c").read_text()
    handles = "\n".join(function(subr, "int\n" + name + "(") for name in
                        ("fdtbus_offset2phandle", "fdtbus_phandle2offset"))
    fixture = r'''
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <errno.h>
#include <libfdt.h>
static unsigned checks, calls, added;
#define CHECK(v) do { checks++; if (!(v)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #v); exit(1); } } while (0)
static unsigned char tree[4096];
static const void *fdt_data = tree;
static const void *fdtbus_get_data(void) { return fdt_data; }
HANDLES
static jmp_buf panic_env;
static char panic_text[128];
static uint64_t start, end;
static _Noreturn void
panic(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(panic_text, sizeof(panic_text), format, args);
    va_end(args);
    longjmp(panic_env, 1);
}
/* Model only register decoding and range publication, not node iteration. */
static int
fdtbus_get_reg64(int phandle, unsigned index, uint64_t *addr, uint64_t *size)
{
    int len;
    const char *reg = fdt_getprop(tree, fdtbus_phandle2offset(phandle), "reg", &len);
    if (reg == NULL || len <= 0 || index >= (unsigned)len / 16)
        return EINVAL;
    fdt64_t value;
    memcpy(&value, reg + index * 16, sizeof(value)); *addr = fdt64_to_cpu(value);
    memcpy(&value, reg + index * 16 + 8, sizeof(value)); *size = fdt64_to_cpu(value);
    return 0;
}
static void
fdt_memory_add_range(uint64_t addr, uint64_t size)
{
    CHECK(addr > 0 && size > 0);
    added++;
}
static int
checked_next(const void *data, int offset, const char *name, const void *value, int len)
{
    /* A host watchdog bounds the old loop. Every result is from real libfdt. */
    if (++calls > 32) {
        fputs("FAIL: FDT memory iterator repeated a parser error\n", stderr);
        exit(3);
    }
    return fdt_node_offset_by_prop_value(data, offset, name, value, len);
}
#define fdt_node_offset_by_prop_value checked_next
MEMORY
#undef fdt_node_offset_by_prop_value
static void
reset(void)
{
    memset(tree, 0, sizeof(tree));
    CHECK(fdt_create_empty_tree(tree, sizeof(tree)) == 0);
}
static void
memory_node(const char *name, bool typed, uint64_t addr, uint64_t size)
{
    int off = fdt_add_subnode(tree, 0, name); CHECK(off >= 0);
    fdt64_t reg[] = {cpu_to_fdt64(addr), cpu_to_fdt64(size)};
    CHECK(fdt_setprop(tree, off, "reg", reg, sizeof(reg)) == 0);
    if (typed)
        CHECK(fdt_setprop(tree, off, "device_type", "memory", sizeof("memory")) == 0);
    CHECK(fdt_check_full(tree, sizeof(tree)) == 0);
}
static void
run_case(const char *expected, unsigned expected_added)
{
    calls = added = 0; start = end = 0; panic_text[0] = '\0';
    if (setjmp(panic_env) == 0) {
        fdt_memory_get(&start, &end);
        CHECK(expected == NULL);
    } else {
        CHECK(expected != NULL);
        CHECK(strcmp(panic_text, expected) == 0);
    }
    CHECK(added == expected_added);
    CHECK(calls <= 4);
}
static void
malformed(bool tail)
{
    reset();
    int off = 0;
    if (tail) {
        CHECK(fdt_add_subnode(tree, 0, "tail") >= 0);
        memory_node("memory@40000000", true, 0x40000000, 0x10000000);
        off = fdt_path_offset(tree, "/tail"); CHECK(off > 0);
        CHECK(fdt_path_offset(tree, "/memory") < off);
    }
    fdt32_t bad = cpu_to_fdt32(0xdeadbeef);
    memcpy(tree + fdt_off_dt_struct(tree) + off, &bad, sizeof(bad));
    CHECK(fdt_check_header(tree) == 0);
    CHECK(fdt_check_full(tree, sizeof(tree)) == -FDT_ERR_BADSTRUCTURE);
    run_case("fdt_memory_get: FDT_ERR_BADSTRUCTURE", tail ? 1 : 0);
}
int
main(int argc, char **argv)
{
    if (argc == 2) {
        CHECK(strcmp(argv[1], "first") == 0 || strcmp(argv[1], "tail") == 0);
        malformed(strcmp(argv[1], "tail") == 0);
    } else {
        CHECK(argc == 1);
        reset(); memory_node("memory@40000000", true, 0x40000000, 0x10000000);
        run_case(NULL, 1); CHECK(start == 0x40000000 && end == 0x50000000);
        memory_node("memory@100000000", true, UINT64_C(0x100000000), 0x20000000);
        run_case(NULL, 2); CHECK(start == 0x40000000 && end == UINT64_C(0x120000000));
        reset(); memory_node("memory", false, 0x40000000, 0x10000000);
        run_case(NULL, 1); CHECK(start == 0x40000000 && end == 0x50000000);
        reset(); run_case("Cannot determine memory size", 0);
        memory_node("memory", true, 0x40000000, 0);
        run_case("Cannot determine memory size", 0);
        malformed(false); malformed(true);
        reset(); fdt_set_magic(tree, 0);
        run_case("fdt_memory_get: FDT_ERR_BADMAGIC", 0);
    }
    printf("PASS: %u actual-source FDT memory checks\n", checks);
    return 0;
}
'''.replace("HANDLES", handles).replace("MEMORY", memory)
    (work / "contract.c").write_text(fixture)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--emit", type=Path)
    parser.add_argument("--case", choices=("first", "tail"))
    args = parser.parse_args()
    if args.emit:
        args.emit.mkdir(parents=True, exist_ok=True)
        emit(args.source_root, args.emit)
        return
    with tempfile.TemporaryDirectory(prefix="fdt-memory-") as tmp:
        work = Path(tmp)
        emit(args.source_root, work)
        build_and_run(args.source_root, work,
                      shlex.split(os.environ.get("FDT_MEMORY_CFLAGS", "")),
                      [args.case] if args.case else [])


if __name__ == "__main__":
    main()
