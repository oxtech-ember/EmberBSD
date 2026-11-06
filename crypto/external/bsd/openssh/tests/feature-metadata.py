#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check selected native OpenSSH feature routing using an existing nbmake."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for name in ('nbmake', 'source', 'prepared', 'config-object', 'autoconf'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
root = args.source / 'crypto/external/bsd/openssh'
common = ['EMBER_OPENSSH_SOURCE=' + str(args.prepared),
          'OPENSSH_CONFIG_OBJECT=' + str(args.config_object),
          'HOST_AUTOCONF=' + str(args.autoconf)]
lib = root / 'lib'
selected = subprocess.run([str(args.nbmake), '-C', str(lib), '-V', '${SRCS}',
                           '-V', '${.PATH}', *common], check=True, text=True,
                          stdout=subprocess.PIPE)
srcs, search = selected.stdout.splitlines()
sources = srcs.split()
directories = [Path(p) if Path(p).is_absolute() else lib / p for p in search.split()]
missing = [name for name in sources if not any((p / name).is_file() for p in directories)]
assert not missing, f'selected libssh sources missing from make search path: {missing}'
assert len(sources) == len(set(sources)), 'duplicate selected libssh sources'
vendored = subprocess.run([str(args.nbmake), '-C', str(lib), '-V', '${SRCS}',
                           'EMBER_OPENSSH_SOURCE='], check=True, text=True,
                          stdout=subprocess.PIPE)
assert 'getexecpath.c' in vendored.stdout.split()
for enabled in (True, False):
    for program in ('bin/sshd', 'libexec/sshd-auth', 'libexec/sshd-session'):
        cmd = [str(args.nbmake), '-C', str(root / program),
               '-V', '${USE_LDAP}', '-V', '${CPPFLAGS}', '-V', '${SRCS}',
               '-V', '${LDADD}', *common, 'MKLDAP=' + ('yes' if enabled else 'no')]
        result = subprocess.run(cmd, check=True, text=True, stdout=subprocess.PIPE)
        use_ldap, cppflags, srcs, ldadd = result.stdout.splitlines()
        assert (use_ldap != 'no') == enabled
        assert ('-DWITH_NETBSD_LPK' in cppflags.split()) == enabled
        assert ('ldapauth.c' in srcs.split()) == enabled
        assert ('-lldap' in ldadd.split()) == enabled
        assert ('-DWITH_NETBSD_BLOCKLIST' in cppflags.split()) == (program != 'bin/sshd')
for program in ('libexec/sshd-auth', 'libexec/sshd-session'):
    cmd = [str(args.nbmake), '-C', str(root / program), '-V', '${CPPFLAGS}',
           '-V', '${LDADD}', *common, 'CRUNCHEDPROG=yes']
    cppflags, ldadd = subprocess.run(cmd, check=True, text=True,
                                     stdout=subprocess.PIPE).stdout.splitlines()
    assert '-DSMALL' in cppflags.split()
    assert '-DWITH_NETBSD_BLOCKLIST' in cppflags.split()
    assert '-lblocklist' not in ldadd.split()
legacy = subprocess.run([str(args.nbmake), '-C', str(root / 'bin/sshd'),
                         '-V', '${USE_LDAP}', 'EMBER_OPENSSH_SOURCE=', 'obj'], check=True, text=True,
                        stdout=subprocess.PIPE)
assert legacy.stdout.strip() == 'no'
print('OpenSSH selected feature metadata: PASS')
