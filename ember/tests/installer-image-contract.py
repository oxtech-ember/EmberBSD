#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: AI-assisted regression for the OS installation-image checker.
"""Structural archive controls; this does not accept an ISO or a guest install."""
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('installer_image', Path(__file__).with_name('installer-image.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ImageChecks(unittest.TestCase):
    def test_contract_and_negative_controls(self):
        files = {
            'boot.cfg': b'banner=EmberBSD 1.0 installation\nmenu=Install EmberBSD:boot netbsd -z\nmenu=Install EmberBSD (diagnostic output):boot netbsd -vx\ntimeout=10\nclear=1\n',
            'etc/openssl/cert.pem': b'-----BEGIN CERTIFICATE-----\nfixture',
            'usr/sbin/sysinst': b'github.com\0EmberBSD/amd64 1.0\0Bootable USB / image\0EmberBSD CDN\0Additional sources',
        }
        for name in ('base', 'etc', 'kern-GENERIC'):
            files[f'amd64/binary/sets/{name}.tar.xz'] = b'fixture'
        for lang in ('de', 'es', 'fr', 'pl'):
            files[f'usr/share/sysinst/catalog/sysinstmsgs.{lang}'] = b'MSGTXTS\0EmberBSD-1.0\0Build: fixture'
        controls = [None, ('boot.cfg', b'banner=EmberBSD 2.0 installation\n'),
                    ('boot.cfg', files['boot.cfg'] + b'banner=NetBSD CD\n'),
                    ('etc/openssl/cert.pem', b'empty'), ('usr/sbin/sysinst', b'wrong source'),
                    ('usr/sbin/sysinst', files['usr/sbin/sysinst'] + b'CD-ROM / DVD'),
                    ('usr/share/sysinst/catalog/sysinstmsgs.de', b'MSGTXTS\0EmberBSD-1.0\0Build: foreign'),
                    ('amd64/binary/sets/etc.tar.xz', None)]
        with tempfile.TemporaryDirectory(prefix='ember-image-contract-') as temporary:
            path = Path(temporary) / 'image.tar'
            for control in controls:
                current = dict(files)
                if control:
                    if control[1] is None: current.pop(control[0])
                    else: current[control[0]] = control[1]
                with tarfile.open(path, 'w') as archive:
                    for name, data in current.items():
                        member = tarfile.TarInfo(name)
                        member.size = len(data)
                        archive.addfile(member, io.BytesIO(data))
                with self.subTest(control=control):
                    if control:
                        with self.assertRaises(ValueError): checker.inspect(str(path), 'fixture', '1.0', 'github.com')
                    else:
                        checker.inspect(str(path), 'fixture', '1.0', 'github.com')


if __name__ == '__main__':
    unittest.main()
