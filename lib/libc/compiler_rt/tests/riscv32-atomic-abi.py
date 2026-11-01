#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Compile and link the selected RV32 libc atomic ABI provider."""
import argparse
from pathlib import Path
import re
import subprocess
import textwrap


def symbols(nm, path, undefined=False):
    args = [nm, '-g']
    args += ['--undefined-only' if undefined else '--defined-only', str(path)]
    output = subprocess.run(args, check=True, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    names = set()
    for line in output.splitlines():
        fields = line.split()
        if fields:
            name = fields[-1]
            if name.startswith('___atomic'):
                name = name[1:]
            names.add(name)
    return names


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--cc', required=True)
    cli.add_argument('--nm', required=True)
    cli.add_argument('--atomic-source', type=Path, required=True)
    cli.add_argument('--os-source', type=Path, required=True)
    cli.add_argument('--sysroot', type=Path, required=True)
    cli.add_argument('--out-dir', type=Path, required=True)
    args = cli.parse_args()
    args.out_dir.mkdir(parents=True)

    makefile = (args.os_source / 'lib/libc/compiler_rt/Makefile.inc').read_text()
    selection = re.search(r'(?ms)^\.if \$\{LIBC_MACHINE_ARCH\} == "riscv32"\n(.*?)^\.endif', makefile)
    if selection is None or 'GENERIC_SRCS+= atomic.c' not in selection.group(1) or \
            'COMPILER_RT_ATOMIC_BUILTIN_EXCLUDE_1_2_4' not in selection.group(1):
        raise AssertionError('RV32 libc does not select the compiler-rt 8-byte/generic provider')

    target = ['--target=riscv32--netbsd', '-march=rv32imac_zicsr_zifencei',
              '-mabi=ilp32', '--sysroot=' + str(args.sysroot)]
    provider = args.out_dir / 'compiler_rt_atomic.o'
    subprocess.run([args.cc, *target, '-std=c11', '-O2', '-Wno-unused-parameter',
                    '-Wno-deprecated-declarations',
                    '-DCOMPILER_RT_ATOMIC_BUILTIN_EXCLUDE_1_2_4',
                    '-I', str(args.atomic_source.parent), '-c', str(args.atomic_source),
                    '-o', str(provider)], check=True)
    provider_symbols = symbols(args.nm, provider)
    expected = {'__atomic_is_lock_free', '__atomic_load', '__atomic_store',
                '__atomic_compare_exchange', '__atomic_fetch_add_8',
                '__atomic_fetch_and_8', '__atomic_fetch_or_8',
                '__atomic_fetch_sub_8', '__atomic_load_8', '__atomic_store_8'}
    missing = expected - provider_symbols
    duplicate_small = {name for name in provider_symbols if name.startswith('__atomic_') and
                       name.rsplit('_', 1)[-1] in {'1', '2', '4'}}
    if missing or duplicate_small:
        raise AssertionError('RV32 provider ABI mismatch: missing=' + repr(sorted(missing)) +
                             ' duplicate-small=' + repr(sorted(duplicate_small)))

    consumer = args.out_dir / 'wpa-atomic-consumer.c'
    consumer.write_text(textwrap.dedent('''\
        #include <stdint.h>
        static volatile uint64_t shared;
        void wpa_atomic_consumer(void) {
            uint64_t value;
            (void)__atomic_fetch_add(&shared, 1, __ATOMIC_SEQ_CST);
            (void)__atomic_fetch_and(&shared, UINT64_MAX, __ATOMIC_SEQ_CST);
            (void)__atomic_fetch_or(&shared, 1, __ATOMIC_SEQ_CST);
            (void)__atomic_fetch_sub(&shared, 1, __ATOMIC_SEQ_CST);
            __atomic_load(&shared, &value, __ATOMIC_SEQ_CST);
            __atomic_store(&shared, &value, __ATOMIC_SEQ_CST);
            (void)__atomic_is_lock_free(sizeof(shared), (const void *)&shared);
        }
    '''))
    consumer_object = args.out_dir / 'wpa-atomic-consumer.o'
    subprocess.run([args.cc, *target, '-std=c11', '-O0', '-c', str(consumer),
                    '-o', str(consumer_object)], check=True)
    unresolved = symbols(args.nm, consumer_object, undefined=True)
    expected_undefined = {'__atomic_fetch_add_8', '__atomic_fetch_and_8',
                          '__atomic_fetch_or_8', '__atomic_fetch_sub_8',
                          '__atomic_load_8', '__atomic_store_8', '__atomic_is_lock_free'}
    if not expected_undefined <= unresolved:
        raise AssertionError('consumer did not exercise expected ABI calls: ' +
                             repr(sorted(expected_undefined - unresolved)))

    libc = args.sysroot / 'usr/lib/libc.a'
    small_abi = {name for name in symbols(args.nm, libc)
                 if name.startswith('__atomic_') and name.rsplit('_', 1)[-1] in {'1', '2', '4'}}
    if len(small_abi) < 23:
        raise AssertionError('existing libc small atomic ABI inventory unexpectedly shrank')
    linked = args.out_dir / 'wpa-atomic-consumer-linked.o'
    subprocess.run([args.cc, *target, '-nostdlib', '-fuse-ld=lld', '-Wl,-r',
                    str(provider), str(consumer_object), str(libc), '-o', str(linked)], check=True)
    remaining = symbols(args.nm, linked, undefined=True)
    missing_after_link = expected_undefined - remaining
    if missing_after_link != expected_undefined:
        still_unresolved = expected_undefined & remaining
        raise AssertionError('consumer still has atomic ABI references: ' + repr(sorted(still_unresolved)))
    print('PASS: RV32 libc provider ABI, existing small symbols, and consumer relocatable link')


if __name__ == '__main__':
    main()
