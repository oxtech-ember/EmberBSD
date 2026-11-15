#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; AI-assisted native Heimdal generation regression.
"""Exercise real OS make targets and native generators below two source roots."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--nbmake', required=True, type=Path)
p.add_argument('--source', required=True, type=Path)
p.add_argument('--work-dir', required=True, type=Path)
p.add_argument('--os', type=Path, default=Path(__file__).resolve().parents[7])
p.add_argument('--previous-rules', type=Path,
               help='previous Makefile.rules.inc for a retained-object upgrade regression')
a = p.parse_args()
os_root = a.os.resolve(strict=True)
heimdal = os_root / 'crypto/external/bsd/heimdal'
outputs = {'libasn1': ['krb5_asn1.h', 'pkcs8_asn1.h', 'asn1_err.h',
                       'asn1_krb5_asn1.c', 'asn1_pkcs8_asn1.c', 'asn1_err.c'],
           'libkrb5': ['krb5_err.h', 'krb5_err.c']}
with tempfile.TemporaryDirectory(prefix='heimdal-paths-', dir=a.work_dir) as temporary:
    root = Path(temporary).resolve()
    results = []
    for index, (mode, nested) in enumerate((('yes', False), ('yes', False),
                                           ('yes', True), ('no', False))):
        host = root / ('host-' + str(index)) / ('deep-' * (index + 1))
        host.mkdir(parents=True)
        source = host / 'prepared.heimdal+source'
        source.symlink_to(a.source.resolve(strict=True), target_is_directory=True)
        netbsd_source = os_root
        if nested:
            # A source-backed tree with the prepared input inside NETBSDSRCDIR.
            # Do not create fixtures in the caller's actual checkout.
            for child in os_root.iterdir():
                if child.is_dir() and not child.name.startswith('.'):
                    (host / child.name).symlink_to(child, target_is_directory=True)
            netbsd_source = host
        generated = {}
        for library, names in outputs.items():
            obj = host / library
            obj.mkdir()
            command = [str(a.nbmake.resolve(strict=True)), '-j10', '-m', str(os_root / 'share/mk'),
                       '-C', str(heimdal / 'lib' / library),
                       'NETBSDSRCDIR=' + str(netbsd_source), 'MAKEOBJDIR=' + str(obj),
                       'EMBER_HEIMDAL_SOURCE=' + str(source), 'MKREPRO=' + mode,
                       'MKCLEANSRC=no']
            query = subprocess.run(command + ['-V', '.OBJDIR'], check=True,
                                   capture_output=True, text=True)
            if query.stdout.strip() != str(obj):
                raise SystemExit('OS wrapper did not select the private object directory')
            result = subprocess.run(command + names, capture_output=True, text=True)
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            if str(source) not in result.stdout:
                raise SystemExit('generator commands lost their actual source paths')
            # Re-enter through the complete prerequisite set used by dependall.
            dpsrcs = subprocess.run(command + ['-V', '${DPSRCS}'], check=True,
                                   capture_output=True, text=True).stdout
            subprocess.run(command + ['clean'], check=True, capture_output=True, text=True)
            result = subprocess.run(command + sorted(set(shlex.split(dpsrcs)) | set(names)),
                                    capture_output=True, text=True)
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            for name in names:
                data = (obj / name).read_bytes()
                if mode == 'yes':
                    if str(root).encode() in data:
                        raise SystemExit('generated public artifact leaks a host path: ' + name)
                    if b'/usr/src/external/heimdal/' not in data:
                        raise SystemExit('generated artifact lost its canonical source path: ' + name)
                elif str(source).encode() not in data:
                    raise SystemExit('MKREPRO=no unexpectedly rewrote source paths: ' + name)
                generated[library + '/' + name] = data
            if library == 'libasn1':
                if str(source).encode() not in (obj / 'krb5_asn1.hx').read_bytes():
                    raise SystemExit('private upstream ASN1 input was rewritten')
        if mode == 'yes':
            results.append(generated)
    if any(result != results[0] for result in results[1:]):
        raise SystemExit('generated public artifacts differ between source roots')

    if a.previous_rules:
        integration = root / 'integration'
        integration.mkdir()
        for child in heimdal.iterdir():
            if child.name != 'Makefile.rules.inc':
                (integration / child.name).symlink_to(child, target_is_directory=child.is_dir())
        rules = integration / 'Makefile.rules.inc'
        rules.write_bytes(a.previous_rules.read_bytes())
        retained = []
        for library, names in outputs.items():
            obj = root / ('retained-' + library)
            obj.mkdir()
            command = [str(a.nbmake.resolve(strict=True)), '-m', str(os_root / 'share/mk'),
                       '-C', str(heimdal / 'lib' / library), 'NETBSDSRCDIR=' + str(os_root),
                       'HEIMBASE=' + str(integration), 'MAKEOBJDIR=' + str(obj),
                       'EMBER_HEIMDAL_SOURCE=' + str(a.source.resolve(strict=True)), 'MKREPRO=yes']
            result = subprocess.run(command + ['-j1', *names], capture_output=True, text=True)
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            for name in names:
                if str(a.source.resolve()).encode() not in (obj / name).read_bytes():
                    raise SystemExit('previous rules did not reproduce the unnormalized artifact: ' + name)
            retained.append((command, obj, names))
        # Model an ordinary pull after older objects were generated. No clean,
        # source/tool change or clock sleeps; only the selected rule is replaced.
        old_time = time.time_ns() - 10_000_000_000
        for _, obj, _ in retained:
            for generated in obj.iterdir():
                if generated.is_file():
                    os.utime(generated, ns=(old_time, old_time))
        private = root / 'retained-libasn1/krb5_asn1.hx'
        private_before = (private.read_bytes(), private.stat().st_mtime_ns)
        rules.write_bytes((heimdal / 'Makefile.rules.inc').read_bytes())
        stale = []
        for command, obj, names in retained:
            result = subprocess.run(command + ['-j10', *names], capture_output=True, text=True)
            if result.returncode:
                raise SystemExit(result.stdout + result.stderr)
            for name in names:
                data = (obj / name).read_bytes()
                if b'/usr/src/external/heimdal/' not in data or str(a.source.resolve()).encode() in data:
                    stale.append(obj.name + '/' + name)
            before = {name: (obj / name).stat().st_mtime_ns for name in names}
            subprocess.run(command + ['-j10', *names], check=True, capture_output=True, text=True)
            if before != {name: (obj / name).stat().st_mtime_ns for name in names}:
                raise SystemExit('unchanged rule rebuilt already normalized artifacts')
        if private_before != (private.read_bytes(), private.stat().st_mtime_ns):
            raise SystemExit('normalizer update unnecessarily reran the ASN1 generator')
        if stale:
            raise SystemExit('retained objects were not refreshed after a rule update: ' + ', '.join(stale))

    # Failed generation still diagnoses the real input; no stderr scrubber.
    bad = root / 'malformed.asn1'
    bad.write_text('BAD DEFINITIONS ::= BEGIN\nTHIS IS BROKEN\nEND\n')
    command = [str(a.nbmake.resolve(strict=True)), '-m', str(os_root / 'share/mk'),
               '-C', str(heimdal / 'lib/libasn1'), 'NETBSDSRCDIR=' + str(os_root),
               'MAKEOBJDIR=' + str(root), '-V', '${TOOL_ASN1_COMPILE}']
    tool = subprocess.run(command, check=True, capture_output=True, text=True).stdout.strip()
    result = subprocess.run([tool, str(bad), 'malformed'], cwd=root,
                            capture_output=True, text=True)
    if result.returncode == 0 or str(bad) not in result.stderr:
        raise SystemExit('failed ASN1 generation lost its actual source diagnostic')
print('PASS: parallel clean OS DPSRCS targets canonicalize Heimdal headers/C across '
      'two roots and nested prepared source; MKREPRO=no, private inputs and diagnostics preserved')
