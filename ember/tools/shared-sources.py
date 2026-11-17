#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
"""Read ember/shared-sources.json and check the sources of a kernel build.

The file names the Ports producer of each prepared source that the OS can
consume (EMBER_*_SOURCE), and the sources that a kernel consumes. Its source
names must be the EMBER_*_SOURCE names of the REPRO_EXTERNAL_ROOTS table in
share/mk/bsd.sys.mk. "describe" prints the checked file for the SDK build
plan. A kernel build registers the sources that it really reads in
REPRO_EXTERNAL_SOURCE_VARS; "check-kernel" stops the build when they differ
from the file, so the file cannot silently drift from config(1).
"""
import json
from pathlib import Path
import re
import sys

SCHEMA = 'emberbsd.os-shared-sources.v1'
VARIABLE = re.compile(r'EMBER_[A-Z0-9_]+_SOURCE\Z')
PACKAGE = re.compile(r'[a-z0-9][a-z0-9+_.-]*/[A-Za-z0-9][A-Za-z0-9+_.-]*\Z')
PRODUCER = re.compile(r'(ports|sdk):[A-Za-z0-9_][A-Za-z0-9_./+-]*\Z')
CONFIG = re.compile(r'[A-Za-z0-9_]+\Z')


def fail(message):
    raise SystemExit('shared-sources: ' + message)


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail('duplicate key ' + key)
        result[key] = value
    return result


def load(path):
    try:
        doc = json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique)
    except (OSError, UnicodeError, ValueError) as error:
        fail(f'cannot read {path}: {error}')
    if not isinstance(doc, dict) or set(doc) != {'schema', 'sources', 'kernel'} or doc['schema'] != SCHEMA:
        fail('unknown document form')
    sources = doc['sources']
    if not isinstance(sources, dict) or not sources:
        fail('sources must be a non-empty object')
    for name, entry in sources.items():
        if (not VARIABLE.match(name) or not isinstance(entry, dict) or
                set(entry) != {'package', 'producer'} or
                not isinstance(entry['package'], str) or not PACKAGE.match(entry['package']) or
                not isinstance(entry['producer'], str) or not PRODUCER.match(entry['producer']) or
                '..' in entry['producer'].split('/')):
            fail('invalid source ' + name)
    kernel = doc['kernel']
    if not isinstance(kernel, dict) or set(kernel) != {'default', 'configs'} or not isinstance(kernel['configs'], dict):
        fail('kernel must have default and configs')
    for config, names in [('default', kernel['default']), *kernel['configs'].items()]:
        if (not CONFIG.match(config) or not isinstance(names, list) or names != sorted(set(names)) or
                any(name not in sources for name in names)):
            fail('invalid kernel sources for ' + config)
    table_path = Path(path).resolve().parents[1] / 'share/mk/bsd.sys.mk'
    try:
        table = re.search(r'^\.for _var _path in (.*?)^\.if', table_path.read_text(encoding='utf-8'), re.M | re.S)
    except (OSError, UnicodeError) as error:
        fail(f'cannot read {table_path}: {error}')
    if table is None:
        fail('share/mk/bsd.sys.mk has no REPRO_EXTERNAL_ROOTS table')
    names = {word for word in table.group(1).replace('\\', ' ').split() if VARIABLE.match(word)}
    if names != set(sources):
        fail('the sources differ from the bsd.sys.mk table: ' + ' '.join(sorted(names ^ set(sources))))
    return doc


def kernel_sources(doc, config):
    return doc['kernel']['configs'].get(config, doc['kernel']['default'])


def main(argv):
    if len(argv) == 2 and argv[0] == 'describe':
        sys.stdout.write(json.dumps(load(argv[1]), sort_keys=True) + '\n')
        return 0
    if len(argv) < 3 or argv[0] != 'check-kernel':
        fail('usage: shared-sources.py describe FILE | check-kernel FILE CONFIG [REGISTERED...]')
    doc = load(argv[1])
    config = argv[2]
    if not CONFIG.match(config):
        fail('invalid kernel configuration ' + config)
    declared = set(kernel_sources(doc, config))
    registered = set(argv[3:])
    if registered != declared:
        fail(f'{config} reads {" ".join(sorted(registered)) or "no prepared source"}, '
             f'but ember/shared-sources.json declares {" ".join(sorted(declared)) or "none"}; '
             f'add or correct kernel.configs.{config}')
    print(f'{config}: prepared sources {" ".join(sorted(registered))} match ember/shared-sources.json')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
