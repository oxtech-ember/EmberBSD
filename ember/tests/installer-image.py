#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD complete amd64 ISO artifact regression, 2026-10-13.
# AI-assisted Python translation of installer-image.rb; same artifact checks.
"""Check the actual installation image through the host libarchive tar tool."""
import argparse
import re
import subprocess
import sys


def check(condition, message):
    if not condition:
        raise ValueError(message)


def inspect(image, build=None, product_version='1.0', set_host='cdn.emberbsd.oxteam.me'):
    def tar(*args):
        result = subprocess.run(['tar', *args], capture_output=True, check=False)
        check(result.returncode == 0, result.stderr.decode('utf-8', 'replace'))
        return result.stdout

    def extract(path):
        return tar('-xOf', image, path)

    config = extract('boot.cfg').decode('utf-8')
    trust = extract('etc/openssl/cert.pem')
    check(b'-----BEGIN CERTIFICATE-----' in trust, 'ISO has no HTTPS trust anchors')
    check(re.search(r'^banner=EmberBSD', config, re.M), 'ISO lost its EmberBSD installation banner')
    check(f'banner=EmberBSD {product_version} installation' in config, 'ISO presents the wrong EmberBSD product version')
    check(not re.search(r'^banner=.*\b(?:CD|DVD)\b', config, re.M), 'ISO presents optical media as the installation method')
    required = {
        'menu=Install EmberBSD:boot netbsd -z\n': 'ISO lost its quiet installation default',
        'menu=Install EmberBSD (diagnostic output):boot netbsd -vx\n': 'ISO lost its diagnostic installation choice',
        'timeout=10\n': 'ISO does not use the requested 10-second countdown',
        'clear=1\n': 'ISO does not clear the mascot screen',
    }
    for line, message in required.items():
        check(line in config.splitlines(keepends=True), message)
    check(not re.search(r'^banner=.*NetBSD', config, re.M), 'ISO shows a prominent upstream banner')
    names = tar('-tf', image).decode().splitlines()
    for name in ('base', 'etc', 'kern-GENERIC'):
        check(f'amd64/binary/sets/{name}.tar.xz' in names, f'ISO has no {name} set')
    binary = extract('usr/sbin/sysinst')
    check(set_host.encode() in binary, 'ISO installer has the wrong set source')
    check(f'EmberBSD/amd64 {product_version}'.encode() in binary, 'ISO installer has the wrong EmberBSD product version')
    for label in ('Bootable USB / image', 'EmberBSD CDN', 'Additional sources'):
        check(label.encode() in binary, 'ISO installer is missing source choice: ' + label)
    check(b'CD-ROM / DVD' not in binary, 'ISO installer retains the optical source label')
    check(b'CD vs. DVD' not in binary, 'ISO installer recommends optical images for missing sets')
    if build:
        for lang in ('de', 'es', 'fr', 'pl'):
            data = extract(f'usr/share/sysinst/catalog/sysinstmsgs.{lang}')
            check(data.startswith(b'MSGTXTS\0'), lang + ': catalog header')
            check(f'EmberBSD-{product_version}'.encode() in data, lang + ': wrong EmberBSD product version')
            check(f'Build: {build}'.encode() in data, lang + ': wrong image build ID')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image')
    parser.add_argument('build', nargs='?')
    parser.add_argument('product_version', nargs='?', default='1.0')
    parser.add_argument('set_host', nargs='?', default='cdn.emberbsd.oxteam.me')
    args = parser.parse_args()
    try:
        inspect(args.image, args.build, args.product_version, args.set_host)
        print('PASS: actual ISO installation banner, preserved boot command, own sets/server and requested catalog build ID')
        return 0
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
