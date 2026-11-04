#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Exercise build.sh libs routing and the real OS legal install target."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--nbmake', type=Path, required=True)
p.add_argument('--shell', type=Path, required=True)
p.add_argument('--work-dir', type=Path, required=True)
a = p.parse_args()
source = Path(__file__).resolve().parents[2]
nbmake = a.nbmake.resolve(strict=True)
shell = a.shell.resolve(strict=True)
work = a.work_dir.resolve(strict=True)
text = (source / 'build.sh').read_text()
match = re.search(r'^buildlibs\(\)\n\{\n.*?^\}', text, re.M | re.S)
if not match:
    raise SystemExit('buildlibs function missing')
function = match.group(0)
with tempfile.TemporaryDirectory(prefix='libs-legal-', dir=work) as temporary:
    root = Path(temporary)
    for enabled in (True, False):
        dest = root / ('enabled' if enabled else 'disabled')
        (dest / 'usr/share/legal').mkdir(parents=True)
        log = dest / 'calls'
        script = '''
make_in_dir() {
    printf '%s %s\\n' "$1" "$2" >> "$CALLS"
    if [ "$1" = share/legal ] && [ "$2" = install ]; then
        "$NBMAKE" -C "$SOURCE/share/legal" "DESTDIR=$DEST" \\
            MKUNPRIVED=yes "MKSHARE=$MKSHARE" "METALOG=$DEST/METALOG" install || exit $?
    fi
}
statusmsg() { :; }
''' + function + '\nbuildlibs\n'
        env = dict(os.environ, CALLS=str(log), NBMAKE=str(nbmake), SOURCE=str(source),
                   DEST=str(dest), MKSHARE='yes' if enabled else 'no',
                   MKOBJDIRS='no', MKUPDATE='yes')
        result = subprocess.run([str(shell), '-c', script], env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SystemExit(result.stdout)
        if log.read_text().splitlines() != ['. do-distrib-dirs', '. includes', '. do-lib', 'share/legal install']:
            raise SystemExit('unexpected libs order or missing legal target')
        installed = dest / 'usr/share/legal/COPYRIGHT'
        metalog = dest / 'METALOG'
        if enabled:
            if installed.read_bytes() != (source / 'share/legal/COPYRIGHT').read_bytes():
                raise SystemExit('installed legal notice differs from source')
            rows = metalog.read_text().splitlines()
            if not any(row.startswith('./usr/share/legal/COPYRIGHT ') and 'type=file' in row and
                       'mode=0444' in row for row in rows):
                raise SystemExit('legal install missing METALOG file entry')
            before = installed.stat().st_mtime_ns
            repeat = subprocess.run([str(shell), '-c', script], env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if repeat.returncode or installed.stat().st_mtime_ns != before:
                raise SystemExit('repeated legal install changed the unchanged notice: ' + repeat.stdout)
        elif installed.exists() or (metalog.exists() and metalog.stat().st_size):
            raise SystemExit('MKSHARE=no unexpectedly installed legal notice')
print('build.sh libs legal install and MKSHARE=no: PASS')
