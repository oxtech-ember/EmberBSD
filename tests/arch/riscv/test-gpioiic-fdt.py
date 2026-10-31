#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Developed with AI assistance.
"""Exercise production GPIO I2C adapter callbacks with simulated pins."""
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
            if not depth:
                return source[start + 1:i + 1]
    raise ValueError(name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--clang', type=Path, required=True)
    args = parser.parse_args()
    src = (args.source / 'sys/dev/fdt/gpioiic_fdt.c').read_text()
    funcs = '\n'.join(function(src, x) for x in ('bb_bits', 'bb_dir', 'bb_read'))
    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define SDA 1U
#define SCL 2U
struct fdtbus_gpio_pin { int level; };
struct gpioiic_fdt_softc { struct fdtbus_gpio_pin *sda, *scl; bool sda_input; };
static void fdtbus_gpio_write_raw(struct fdtbus_gpio_pin *p, int v) { p->level = !!v; }
static int fdtbus_gpio_read_raw(struct fdtbus_gpio_pin *p) { return p->level; }
'''
    tests = r'''
int main(void) {
 struct fdtbus_gpio_pin a={0},b={0}; struct gpioiic_fdt_softc sc={&a,&b,0};
 bb_dir(&sc,SDA); bb_bits(&sc,0); assert(a.level==0 && b.level==0);
 bb_dir(&sc,0); assert(a.level==1); bb_bits(&sc,SCL); assert(a.level==1 && b.level==1);
 assert(bb_read(&sc)==(SDA|SCL)); bb_dir(&sc,SDA); bb_bits(&sc,SCL); assert(a.level==0 && b.level==1);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'test.c'; exe = Path(tmp) / 'test'
        c.write_text(harness + funcs + tests)
        subprocess.run([str(args.clang),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('GPIO I2C callback tests passed')

if __name__ == '__main__':
    main()
