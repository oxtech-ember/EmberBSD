#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.
"""Exercise real Heimdal ASN.1 stamp rules with a controlled generator."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--make', required=True)
args = parser.parse_args()
rules = Path(__file__).resolve().parents[2] / 'crypto/external/bsd/heimdal/Makefile.rules.inc'
with tempfile.TemporaryDirectory(prefix='heimdal-generator-') as temporary:
    root = Path(temporary)
    generator = root / 'generator'
    generator.write_text('#!' + sys.executable + '\n'
        'import os, pathlib, sys\n'
        'with open("calls", "a") as f: f.write(" ".join(sys.argv[1:]) + "\\n")\n'
        'if os.environ.get("FAIL_GENERATOR"): sys.exit(7)\n'
        'base = sys.argv[-1]\n'
        'pathlib.Path(base + ".hx").write_text("generated\\n")\n'
        'pathlib.Path(base + "-priv.hx").write_text("generated-private\\n")\n')
    generator.chmod(0o755)
    for with_options in (False, True):
        work = root / ('options' if with_options else 'plain')
        work.mkdir()
        (work / 'example.asn1').write_text('fixture\n')
        if with_options:
            (work / 'example.opt').write_text('fixture\n')
        (work / 'Makefile').write_text('HEIMSRCS= example.asn1\nTOOL_ASN1_COMPILE= ' + str(generator) +
            '\nTOOL_SED= /usr/bin/sed\n.include "' + str(rules) + '"\n')
        def run(fail=False):
            env = dict(os.environ)
            if fail:
                env['FAIL_GENERATOR'] = '1'
            else:
                env.pop('FAIL_GENERATOR', None)
            return subprocess.run([args.make, '-r', '-j4', 'example_asn1.h'], cwd=work,
                                  env=env, text=True, capture_output=True)
        failed = run(True)
        assert failed.returncode != 0, failed.stdout + failed.stderr
        assert not (work / 'example.ts').exists()
        success = run()
        assert success.returncode == 0, success.stdout + success.stderr
        assert (work / 'example.ts').is_file()
        assert (work / 'example_asn1.h').read_text() == 'generated\n'
        calls = (work / 'calls').read_text().splitlines()
        assert len(calls) == 2 and (('--option-file=' in calls[-1]) == with_options), calls
        again = run()
        assert again.returncode == 0, again.stdout + again.stderr
        assert (work / 'calls').read_text().splitlines() == calls
print('Heimdal ASN.1 generation failure/retry PASS')
