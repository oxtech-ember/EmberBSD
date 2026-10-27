#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Check bmake's direct `@true` recipe with only the SDK routing on PATH."""

import argparse
from pathlib import Path
import os
import shutil
import subprocess


def run_recipe(make, makefile, path):
    env = dict(os.environ, PATH=str(path), LC_ALL='C')
    return subprocess.run([str(make), '-f', str(makefile)], cwd=makefile.parent,
                          env=env, text=True, capture_output=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--make', type=Path, required=True,
                        help='selected SDK bmake executable')
    parser.add_argument('--routing', type=Path, required=True,
                        help='SDK-published native OS routing directory')
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    routing = args.routing.resolve(strict=True)
    make = args.make.resolve(strict=True)
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    makefile = work / 'Makefile'
    makefile.write_text('all:\n\t@true\n')

    true_link = routing / 'true'
    if not true_link.is_symlink():
        raise RuntimeError('SDK routing has no true command symlink')
    positive = run_recipe(make, makefile, routing)
    if positive.returncode != 0:
        raise RuntimeError('selected bmake @true failed with SDK routing: '
                           + positive.stdout + positive.stderr)

    baseline = work / 'routing-without-true'
    baseline.mkdir()
    for member in routing.iterdir():
        if member.name == 'true':
            continue
        target = baseline / member.name
        if member.is_symlink():
            os.symlink(os.readlink(member), target)
        elif member.is_file():
            shutil.copyfile(member, target)
    negative = run_recipe(make, makefile, baseline)
    if negative.returncode == 0:
        raise RuntimeError('baseline unexpectedly ran @true without a routed true command')
    if 'true' not in (negative.stdout + negative.stderr).lower():
        raise RuntimeError('baseline failed for a reason other than missing true: '
                           + negative.stdout + negative.stderr)

    restored = run_recipe(make, makefile, routing)
    if restored.returncode != 0:
        raise RuntimeError('restored SDK routing failed @true: '
                           + restored.stdout + restored.stderr)
    print('selected bmake @true passes with SDK routing, fails without true, and passes after restore')


if __name__ == '__main__':
    main()
