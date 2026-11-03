#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Check OpenSSL refresh ownership with the SDK planner."""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile
parser = argparse.ArgumentParser()
parser.add_argument("--sdk-source", type=pathlib.Path, required=True)
parser.add_argument("--prefix", type=pathlib.Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.sdk_source / "src"))
from emberbsd_sdk import prepared_refresh
from emberbsd_sdk.errors import SDKError
source = pathlib.Path(__file__).resolve().parents[2]
contract = json.loads((source / prepared_refresh.CONTRACT).read_text())
entry = contract['sources']['EMBER_OPENSSL_SOURCE']
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp) / 'os'
    obj = pathlib.Path(tmp) / 'obj'
    contract_path = root / prepared_refresh.CONTRACT
    contract_path.parent.mkdir(parents=True)
    contract_path.write_text(json.dumps(contract))
    for name in entry['producer_dirs']:
        (root / name).mkdir(parents=True, exist_ok=True)
        (root / name / 'Makefile').touch()
        (obj / name).mkdir(parents=True, exist_ok=True)
    nested = obj / 'crypto/external/apache2/openssl/generated'
    nested.mkdir()
    (nested / 'outside').symlink_to('/unrelated', target_is_directory=True)
    unrelated = obj / 'unrelated'
    unrelated.mkdir()
    (unrelated / 'keep.o').write_bytes(b'object')
    (unrelated / 'keep.d').write_text('keep.o: /source/other.h\n')
    outputs = []
    result = prepared_refresh.plan(root, obj, ['openssl'], nested_outputs=outputs)
    assert set(result) == set(entry['producer_dirs']), result
    assert outputs == [str(nested)] and (unrelated / 'keep.o').exists()
    contract['sources']['EMBER_OPENSSL_SOURCE']['producer_dirs'] = ['../outside']
    contract_path.write_text(json.dumps(contract))
    try:
        prepared_refresh.plan(root, obj, ['openssl'])
    except SDKError:
        pass
    else:
        raise AssertionError('unsafe path accepted')
print('OpenSSL owner contract: producer scope, generated owner, unrelated object, unsafe-path refusal PASS')

with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp).resolve()
    obj = root / "obj"
    obj.mkdir()
    prepared = root / "prepared"
    prepared.mkdir()
    generated = obj / "generated"
    generated.mkdir()
    (generated / ".owned").write_text(json.dumps({"format": "emberbsd-openssl-generated", "object_root": str(obj)}))
    (generated / "payload").touch()
    base = [str(args.prefix / "bin/bmake"), "-r", "-m", str(source / "share/mk"),
            "-C", str(source / "crypto/external/apache2/openssl"),
            "MAKEOBJDIR=" + str(obj), "NOSUBDIR=yes", "MKCLEANSRC=no",
            "EMBER_OPENSSL_SOURCE=" + str(prepared),
            "HOST_PYTHON=" + str(args.prefix / "bin/python3.15"),
            "HOST_PERL=" + str(args.prefix / "bin/perl"),
            "HOST_GMAKE=" + str(args.prefix / "bin/gmake"),
            "HOST_SH=" + str(args.prefix / "bin/mksh")]
    env = dict(os.environ, PATH=str(args.prefix / "bin") + ":" + str(args.prefix / "gnu/bin"), SHELL=str(args.prefix / "bin/mksh"))
    subprocess.run(base + ["EMBER_OPENSSL_GENERATED=" + str(generated), "clean"], env=env, check=True)
    assert not generated.exists() and prepared.is_dir()
    refused = subprocess.run(base + ["EMBER_OPENSSL_GENERATED=" + str(prepared), "clean"],
                             check=False, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    assert refused.returncode != 0 and prepared.is_dir()
print("OpenSSL actual bmake clean and unsafe override guard PASS")

owner = source / 'crypto/external/apache2/openssl'
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp).resolve()
    src, obj, prepared = root / 'src', root / 'obj', root / 'prepared'
    for directory in (src, obj, prepared):
        directory.mkdir()
    (src / 'tools').symlink_to(owner / 'tools')
    (src / 'Makefile').write_text(f'.include "{owner / "Makefile"}"\n')
    for child in ('first', 'second'):
        directory = src / child
        directory.mkdir()
        (directory / 'Makefile').write_text(
            f'.include "{obj / "generated/common.mk"}"\n'
            'clean cleandir:\n'
            f'\t${{HOST_PYTHON}} -B -c "from pathlib import Path; '
            f'Path(\'{root / child}\').touch()"\n')
    env = dict(os.environ, PATH=f'{args.prefix}/bin:{args.prefix}/gnu/bin',
               SHELL=str(args.prefix / 'bin/mksh'))
    command = [str(args.prefix / 'bin/bmake'), '-r', '-j2', '-m',
               str(source / 'share/mk'), '-C', str(src),
               f'MAKEOBJDIR={obj}', 'MKCLEANSRC=no', 'SUBDIR=first second',
               f'EMBER_OPENSSL_SOURCE={prepared}',
               f'EMBER_OPENSSL_GENERATED={obj / "generated"}',
               f'HOST_PYTHON={args.prefix}/bin/python3.15',
               f'HOST_PERL={args.prefix}/bin/perl',
               f'HOST_GMAKE={args.prefix}/bin/gmake',
               f'HOST_SH={args.prefix}/bin/mksh']
    for target in ('clean', 'cleandir'):
        for child in ('first', 'second'):
            (root / child).unlink(missing_ok=True)
        generated = obj / 'generated'
        generated.mkdir()
        (generated / '.owned').write_text(json.dumps(
            {'format': 'emberbsd-openssl-generated', 'object_root': str(obj)}))
        (generated / 'common.mk').write_text('DUMMY=1\n')
        subprocess.run(command + [target], env=env, check=True)
        assert all((root / child).exists() for child in ('first', 'second'))
        assert not generated.exists() and prepared.is_dir()
print('recursive clean and cleandir -j2 PASS')
