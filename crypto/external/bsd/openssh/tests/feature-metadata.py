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
                         '-V', '${USE_LDAP}', 'obj'], check=True, text=True,
                        stdout=subprocess.PIPE)
assert legacy.stdout.strip() == 'no'
print('OpenSSH selected feature metadata: PASS')
