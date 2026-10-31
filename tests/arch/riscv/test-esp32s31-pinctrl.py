#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Developed with AI assistance.
"""Exercise production pinctrl parsing and MMIO against an in-memory bus."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def function(source, name):
    pos = source.index('\n' + name + '(')
    start = source.rfind('\nstatic ', 0, pos)
    opening = source.index('{', pos)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[start + 1:i + 1]
    raise ValueError(name)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--clang', type=Path, required=True)
    a = p.parse_args()
    source = (a.source / 'sys/arch/riscv/espressif/esp32s31_gpio.c').read_text()
    defs = source[source.index('#define GPIO_OUT_SET'):source.index('static void *esp_gpio_acquire')]
    funcs = '\n'.join(function(source, n) for n in ('esp_gpio_level', 'esp_gpio_enable', 'esp_gpio_pad', 'esp_gpio_route_output', 'esp_gpio_route_input', 'esp_gpio_set_config'))
    root = Path(__file__).parent
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        gen = td / 'test.c'
        gen.write_text((root / 'esp32s31-pinctrl-harness.h').read_text() + '\n' + defs + '\n' + funcs + '\n' + (root / 'esp32s31-pinctrl-harness.inc').read_text())
        subprocess.run([str(a.clang), '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(a.source / 'sys/arch/riscv/espressif'), str(gen), '-o', str(td / 'test')], check=True)
        subprocess.run([str(td / 'test')], check=True)
    print('production pinctrl MMIO tests passed')

if __name__ == '__main__':
    main()
