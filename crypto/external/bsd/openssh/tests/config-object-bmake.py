#!/usr/bin/env python3
"""Check that each OpenSSH OS configuration gets the same inputs."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--bmake', type=Path, required=True)
args = parser.parse_args()
helper = Path(__file__).resolve().parents[1] / 'source.mk'
root = helper.parents[4]


def run(*assignments):
    # -v expands the library paths. source.mk accepts -v only with a prepared
    # source, so give one.
    result = subprocess.run([str(args.bmake), '-f', str(helper),
                             '-f', str(helper.with_name('config.mk')),
                             '-v', 'OPENSSH_OS_CONFIG_NAME',
                             '-v', 'OPENSSH_KRB5_CONFIG_LIBS',
                             '-v', 'OPENSSH_CONFIG_HEADER', '-v', 'CPPFLAGS', '-v', 'DPSRCS',
                             'HOST_AUTOCONF=/autoconf', '_SRC_TOP_OBJ_=/obj',
                             'NETBSDSRCDIR=' + str(root),
                             'DESTDIR=/dest', 'EMBER_OPENSSH_SOURCE=/openssh',
                             *assignments], cwd=root,
                            text=True, capture_output=True)
    assert result.returncode == 0, result.stderr
    name, libraries, header, cppflags, generated = result.stdout.splitlines()
    config = Path('/obj/crypto/external/bsd/openssh/config') / name
    assert Path(header) == config / 'config.h'
    assert str(config / 'compat-headers.json') in generated.split()
    includes = cppflags.split()
    assert includes.index('-I' + str(config)) < includes.index('-I' + str(config / 'build'))
    assert '-I' + str(config / 'compat-include') in includes
    assert '-I' + str(config / 'build/openbsd-compat/include') not in includes
    return name, libraries.split()


# Programs that link dynamically or statically share one configuration,
# so they must give the same target libraries.
dynamic = run('USE_KERBEROS=yes')
static = run('USE_KERBEROS=yes', 'LDSTATIC=-static')
assert dynamic == static
assert dynamic[0] == 'generated'
assert '/dest/usr/lib/libkrb5.so' in dynamic[1]
assert '/dest/usr/lib/libkrb5.a' in dynamic[1]

# Rescue builds without Kerberos. It must not use the configuration of
# the programs with Kerberos.
rescue = run('USE_KERBEROS=no', 'LDSTATIC=-static')
assert rescue == ('generated-no-kerberos', [])

print('OpenSSH OS configuration inputs PASS')
