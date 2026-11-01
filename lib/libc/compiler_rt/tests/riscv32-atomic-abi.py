#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check RV32 libc atomic ABI symbols and link consumer calls to the archive."""
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
            'COMPILER_RT_ATOMIC_BUILTIN_RISCV32_LIBC' not in selection.group(1):
        raise AssertionError('RV32 libc does not select its compiler-rt atomic ABI provider')
    if 'COMPILER_RT_ATOMIC_BUILTIN_RISCV32_LIBC' not in args.atomic_source.read_text():
        raise AssertionError('prepared compiler-rt source lacks the RV32 libc selector')

    target = ['--target=riscv32--netbsd', '-march=rv32imac_zicsr_zifencei',
              '-mabi=ilp32', '--sysroot=' + str(args.sysroot)]
    consumer = args.out_dir / 'wpa-atomic-consumer.c'
    consumer.write_text(textwrap.dedent('''\
        #include <stdbool.h>
        #include <stddef.h>
        #include <stdint.h>
        #define ABI(name) __asm__("__atomic_" #name)
        extern uint8_t load1(const volatile void *, int) ABI(load_1);
        extern uint16_t load2(const volatile void *, int) ABI(load_2);
        extern uint32_t load4(const volatile void *, int) ABI(load_4);
        extern void store1(volatile void *, uint8_t, int) ABI(store_1);
        extern void store2(volatile void *, uint16_t, int) ABI(store_2);
        extern void store4(volatile void *, uint32_t, int) ABI(store_4);
        extern uint8_t fetch_add1(volatile void *, uint8_t, int) ABI(fetch_add_1);
        extern uint16_t fetch_add2(volatile void *, uint16_t, int) ABI(fetch_add_2);
        extern uint32_t fetch_add4(volatile void *, uint32_t, int) ABI(fetch_add_4);
        extern bool compare_exchange1(volatile void *, void *, uint8_t, int,
            int) ABI(compare_exchange_1);
        extern bool compare_exchange2(volatile void *, void *, uint16_t, int,
            int) ABI(compare_exchange_2);
        extern bool compare_exchange4(uint32_t *, uint32_t *, uint32_t, int,
            int) ABI(compare_exchange_4);
        extern uint64_t fetch_add8(volatile void *, uint64_t, int) ABI(fetch_add_8);
        extern uint64_t fetch_and8(volatile void *, uint64_t, int) ABI(fetch_and_8);
        extern uint64_t fetch_or8(volatile void *, uint64_t, int) ABI(fetch_or_8);
        extern uint64_t fetch_sub8(volatile void *, uint64_t, int) ABI(fetch_sub_8);
        extern uint64_t fetch_nand8(volatile void *, uint64_t, int)
            ABI(fetch_nand_8);
        extern uint64_t fetch_xor8(volatile void *, uint64_t, int)
            ABI(fetch_xor_8);
        extern uint64_t exchange8(volatile void *, uint64_t, int)
            ABI(exchange_8);
        extern bool compare_exchange8(uint64_t *, uint64_t *, uint64_t, int, int)
            ABI(compare_exchange_8);
        extern uint64_t load8(const volatile void *, int) ABI(load_8);
        extern void store8(volatile void *, uint64_t, int) ABI(store_8);
        extern int generic_compare_exchange(size_t, void *, void *, void *, int,
            int) __asm__("__atomic_compare_exchange");
        extern void generic_load(size_t, void *, void *, int)
            __asm__("__atomic_load");
        extern void generic_store(size_t, void *, void *, int)
            __asm__("__atomic_store");
        extern bool is_lock_free(size_t, void *) ABI(is_lock_free);
        void wpa_atomic_consumer(void) {
            uint8_t v8 = 0; uint16_t v16 = 0; uint32_t v32 = 0;
            uint64_t v64 = 0, expected = 0;
            (void)load1(&v8, __ATOMIC_ACQUIRE);
            (void)load2(&v16, __ATOMIC_ACQUIRE);
            (void)load4(&v32, __ATOMIC_ACQUIRE);
            store1(&v8, 1, __ATOMIC_RELEASE);
            store2(&v16, 1, __ATOMIC_RELEASE);
            store4(&v32, 1, __ATOMIC_RELEASE);
            (void)fetch_add1(&v8, 1, __ATOMIC_SEQ_CST);
            (void)fetch_add2(&v16, 1, __ATOMIC_SEQ_CST);
            (void)fetch_add4(&v32, 1, __ATOMIC_SEQ_CST);
            (void)compare_exchange1(&v8, &v8, 2,
                __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
            (void)compare_exchange2(&v16, &v16, 2,
                __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
            (void)compare_exchange4(&v32, &v32, 2,
                __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
            (void)fetch_add8(&v64, 1, __ATOMIC_SEQ_CST);
            (void)fetch_and8(&v64, UINT64_MAX, __ATOMIC_SEQ_CST);
            (void)fetch_or8(&v64, 1, __ATOMIC_SEQ_CST);
            (void)fetch_sub8(&v64, 1, __ATOMIC_SEQ_CST);
            (void)fetch_nand8(&v64, UINT64_MAX, __ATOMIC_SEQ_CST);
            (void)fetch_xor8(&v64, 1, __ATOMIC_SEQ_CST);
            (void)exchange8(&v64, 3, __ATOMIC_SEQ_CST);
            (void)compare_exchange8(&v64, &expected, 4,
                __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
            (void)load8(&v64, __ATOMIC_SEQ_CST);
            store8(&v64, 2, __ATOMIC_SEQ_CST);
            (void)generic_compare_exchange(sizeof(v64), &v64, &expected, &v64,
                __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE);
            generic_load(sizeof(v64), &v64, &expected, __ATOMIC_ACQUIRE);
            generic_store(sizeof(v64), &v64, &expected, __ATOMIC_SEQ_CST);
            (void)is_lock_free(sizeof(v64), &v64);
        }
    '''))
    consumer_object = args.out_dir / 'wpa-atomic-consumer.o'
    subprocess.run([args.cc, *target, '-std=c11', '-O0', '-c', str(consumer),
                    '-o', str(consumer_object)], check=True)
    unresolved = symbols(args.nm, consumer_object, undefined=True)
    expected_undefined = {'__atomic_load_' + width for width in ('1', '2', '4', '8')}
    expected_undefined |= {'__atomic_store_' + width for width in ('1', '2', '4', '8')}
    expected_undefined |= {'__atomic_fetch_add_' + width for width in ('1', '2', '4', '8')}
    expected_undefined |= {'__atomic_compare_exchange_1', '__atomic_compare_exchange_2',
                           '__atomic_compare_exchange_4', '__atomic_fetch_and_8',
                           '__atomic_fetch_or_8', '__atomic_fetch_sub_8',
                           '__atomic_fetch_nand_8', '__atomic_fetch_xor_8',
                           '__atomic_exchange_8', '__atomic_compare_exchange_8',
                           '__atomic_compare_exchange', '__atomic_load',
                           '__atomic_store', '__atomic_is_lock_free'}
    if not expected_undefined <= unresolved:
        raise AssertionError('consumer did not exercise expected ABI calls: ' +
                             repr(sorted(expected_undefined - unresolved)))

    libc = args.sysroot / 'usr/lib/libc.a'
    libc_symbols = symbols(args.nm, libc)
    small_abi = {'__atomic_' + op + '_' + width
                 for op in ('compare_exchange', 'exchange', 'fetch_add', 'fetch_and',
                            'fetch_nand', 'fetch_or', 'fetch_sub', 'fetch_xor', 'load', 'store')
                 for width in ('1', '2', '4')}
    missing_libc = small_abi - libc_symbols
    if missing_libc:
        raise AssertionError('libc lacks fixed-width atomic ABI symbols: ' + repr(sorted(missing_libc)))
    required_provider = {'__atomic_compare_exchange', '__atomic_is_lock_free', '__atomic_load',
                         '__atomic_store', '__atomic_fetch_add_8', '__atomic_fetch_and_8',
                         '__atomic_fetch_nand_8', '__atomic_fetch_or_8',
                         '__atomic_fetch_sub_8', '__atomic_fetch_xor_8',
                         '__atomic_exchange_8', '__atomic_compare_exchange_8',
                         '__atomic_load_8', '__atomic_store_8'}
    if not required_provider <= libc_symbols:
        raise AssertionError('libc lacks generic or 8-byte atomic ABI symbols: ' +
                             repr(sorted(required_provider - libc_symbols)))
    linked = args.out_dir / 'wpa-atomic-consumer-linked.o'
    subprocess.run([args.cc, *target, '-nostdlib', '-fuse-ld=lld', '-Wl,-r',
                    str(consumer_object), str(libc), '-o', str(linked)], check=True)
    remaining = symbols(args.nm, linked, undefined=True)
    still_unresolved = expected_undefined & remaining
    if still_unresolved:
        raise AssertionError('consumer still has atomic ABI references: ' + repr(sorted(still_unresolved)))
    print('PASS: RV32 atomic symbols/call sites resolve from installed libc.a alone')


if __name__ == '__main__':
    main()
