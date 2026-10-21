#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: AI-assisted actual-source loader and wire qualification.
"""Compile native contracts. Do not load firmware or access a device."""
import argparse
import importlib
import os
from pathlib import Path
import re
import shlex
import sys

FUNCTIONS = ('aicwf_fw_cmd', 'aicwf_fw_mem_read', 'aicwf_fw_mem_write',
             'aicwf_fw_bip_hash', 'aicwf_wlan_upload', 'aicwf_fw_options_set',
             'aicwf_fw_bip_verify', 'aicwf_fw_start', 'aicwf_wlan_load',
             'aicwf_fw_bip_runtime_verify', 'aicwf_fw_init')


def projection(source):
    result = []
    for name in FUNCTIONS:
        match = re.search(r'\n' + name + r'\(.*?\n}\n', source, re.S)
        if match is None:
            raise ValueError('actual-source function absent: ' + name)
        kind = 'void' if name == 'aicwf_fw_bip_hash' else 'int'
        result.append('static ' + kind + match.group(0))
    return '\n'.join(result)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk-source', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--original')
    p.add_argument('--cc', default='/usr/bin/cc')
    p.add_argument('--cflags', default='')
    args = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'sys/dev/sdmmc/if_aicwf.c').read_text()
    body = projection(source)
    sdk_source = Path(args.sdk_source).resolve(strict=True) / 'src'
    sys.path.insert(0, str(sdk_source))
    package = importlib.import_module('emberbsd_sdk')
    if package.API_VERSION != 1:
        raise ValueError('unsupported SDK host API')
    process = importlib.import_module('emberbsd_sdk.process')
    paths = importlib.import_module('emberbsd_sdk.paths')
    for module in (package, process, paths):
        if not Path(module.__file__).resolve().is_relative_to(sdk_source):
            raise ValueError('SDK module source differs')
    if not all(callable(getattr(process, name, None)) for name in
               ('capture', 'run_logged', 'sanitized_environment', 'SessionInspector')):
        raise ValueError('required SDK process capability absent')
    out = Path(paths.canonical_new(args.output))
    for protected in (root, args.sdk_source, args.original):
        if protected:
            paths.reject_overlap(out, protected)
    env = process.sanitized_environment()
    env['ASAN_OPTIONS'] = 'abort_on_error=1'
    env['UBSAN_OPTIONS'] = 'halt_on_error=1'
    process.SessionInspector(env=env)
    flags = process.capture(['pkg-config', '--cflags', '--libs', 'openssl'], env=env)
    out.mkdir(mode=0o700)
    tmp = out / 'tmp'
    tmp.mkdir(mode=0o700)
    env['TMPDIR'] = str(tmp)
    (out / 'driver-body.h').write_text(body)
    defines = re.findall(r'^#define[ \t]+AICWF_[^\n]*(?:\\\n[^\n]*)*', source, re.M)
    vht = (root / 'sys/dev/sdmmc/if_aicwf_vht.h').read_text()
    defines += re.findall(r'^#define AICWF_ME_CONFIG_LEN[^\n]*', vht, re.M)
    options = re.search(r'static const uint32_t aicwf_fw_options\[\]\[2\] = \{.*?\n};', source, re.S)
    if options is None:
        raise ValueError('actual options array absent')
    (out / 'driver-values.h').write_text('\n'.join(defines) + '\n' + options.group(0) + '\n')
    for name in ('aicwf-fw-bip-contract', 'aicwf-fw-bip-driver-contract'):
        if name.endswith('driver-contract') and args.original is None:
            continue
        argv = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror']
        argv += shlex.split(args.cflags)
        argv += ['-I' + str(root / 'sys/dev/sdmmc'), '-I' + str(out),
                 str(root / 'ember/tools' / (name + '.c'))]
        argv += shlex.split(flags.stdout.decode()) + ['-o', str(out / name)]
        process.run_logged(argv, out / (name + '-compile.log'), env=env)
        argv = [str(out / name)]
        if args.original:
            argv.append(str(Path(args.original).resolve(strict=True)))
        process.run_logged(argv, out / (name + '.log'), env=env)
    return 0


if __name__ == '__main__':
    sys.dont_write_bytecode = True
    raise SystemExit(main())
