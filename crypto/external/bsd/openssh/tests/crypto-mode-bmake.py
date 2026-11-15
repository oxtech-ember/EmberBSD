#!/usr/bin/env python3
"""Check OpenSSH crypto selection and the built-in mode's PIC boundary."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--bmake', type=Path, required=True)
args = parser.parse_args()
helper = Path(__file__).resolve().parents[1] / 'crypto.mk'
root = helper.parents[4]
makefile_inc = helper.parent / 'Makefile.inc'
sha2_adapter = helper.parent / 'include' / 'emberbsd-openssh-sha2.h'


def run(*assignments):
    return subprocess.run([str(args.bmake), '-f', str(helper), '-V',
                           'OPENSSH_CRYPTO_MODE', *assignments], cwd=root,
                          text=True, capture_output=True)


static_builtin = run('MACHINE_ARCH=riscv32', 'MKPIC=no',
                     'EMBER_OPENSSH_CRYPTO=builtin')
assert static_builtin.returncode == 0, static_builtin.stderr
assert static_builtin.stdout.strip() == 'builtin'

pic_builtin = run('MACHINE_ARCH=riscv32', 'MKPIC=yes',
                  'EMBER_OPENSSH_CRYPTO=builtin')
assert pic_builtin.returncode != 0
assert 'currently requires MKPIC=no' in pic_builtin.stderr

default_openssl = run('MACHINE_ARCH=riscv32', 'MKPIC=yes')
assert default_openssl.returncode == 0, default_openssl.stderr
assert default_openssl.stdout.strip() == 'openssl'

legacy_m68000 = run('MACHINE_ARCH=m68000', 'MKPIC=yes')
assert legacy_m68000.returncode == 0, legacy_m68000.stderr
assert legacy_m68000.stdout.strip() == 'builtin'

# Prepared OpenSSH uses OpenBSD SHA*Init names, while EmberBSD libc exports
# the NetBSD SHA*_Init spelling. Keep this compatibility adapter builtin-only.
integration = makefile_inc.read_text()
assert '.if ${OPENSSH_CRYPTO_MODE} == "builtin" && \\' in integration
assert '-include ${NETBSDSRCDIR}/crypto/external/bsd/openssh/include/emberbsd-openssh-sha2.h' in integration
assert '-include ' not in integration.split('.elif ${OPENSSH_CRYPTO_MODE} == "openssl"', 1)[1]
assert 'defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)' in integration
adapter = sha2_adapter.read_text()
for digest in ('256', '384', '512'):
    for operation in ('Init', 'Update', 'Final'):
        assert f'SHA{digest}_{operation}(' in adapter
        assert f'SHA{digest}{operation}(' in adapter
assert 'typedef union {' in adapter and 'SHA256_CTX sha256;' in adapter and 'SHA512_CTX sha512;' in adapter
assert '#define HAVE_SHA256UPDATE 1' in adapter
assert '#define HAVE_SHA384UPDATE 1' in adapter
assert '#define HAVE_SHA512UPDATE 1' in adapter
assert integration.index('.if ${OPENSSH_CRYPTO_MODE} == "builtin" && \\') < \
       integration.index('.include "${NETBSDSRCDIR}/crypto/external/bsd/openssh/source.mk"')

print('OpenSSH crypto mode and PIC boundary PASS')
