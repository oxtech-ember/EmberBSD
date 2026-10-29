#!/usr/bin/env python3
"""Round-trip long relative symlinks with selected native archive tools."""
import argparse
import os
from pathlib import Path
import stat
import subprocess
import tarfile
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--pax', required=True, type=Path)
p.add_argument('--tar', required=True, type=Path)
p.add_argument('--mtree', required=True, type=Path)
p.add_argument('--etc', required=True, type=Path)
a = p.parse_args()
pax, host_tar = str(a.pax.resolve()), str(a.tar.resolve())
with tempfile.TemporaryDirectory(prefix='pax-symlink-') as t:
    root = Path(t)
    src = root / 'input'
    src.mkdir()
    tail = '__cxx03/__type_traits/is_trivially_lexicographically_comparable.h'
    target = src / 'usr/pkg/llvm/include/c++/v1' / tail
    target.parent.mkdir(parents=True)
    target.write_bytes(b'ordinary header fixture\n')
    target.chmod(0o640)
    link = src / 'usr/include/c++' / tail
    link.parent.mkdir(parents=True)
    long_link = os.path.relpath(target, link.parent)
    assert len(os.fsencode(long_link)) == 101, len(os.fsencode(long_link))
    link.symlink_to(long_link)
    short = link.parent / 'short.h'
    short.symlink_to(link.name)
    paths = [str(p.relative_to(src)) for p in (target, link, short)]
    archive = root / 'fixture.tar'
    def pack(extra):
        return subprocess.run([pax, *extra, '-d', '-w', '-f', str(archive), *paths],
                              cwd=src, capture_output=True, text=True)
    old = pack([])
    if old.returncode == 0 or 'Link name too long for ustar' not in old.stderr:
        raise SystemExit('baseline ustar unexpectedly accepted long link: ' + old.stderr)
    archive.unlink(missing_ok=True)
    new = pack(['--gnu'])
    if new.returncode:
        raise SystemExit('GNU longlink write failed: ' + new.stderr)
    tar_alias = root / 'tar'
    tar_alias.symlink_to(pax)
    for label, tool in [('native pax tar-mode', str(tar_alias)), ('host bsdtar', host_tar)]:
        dest = root / ('extract-' + label.replace(' ', '-'))
        dest.mkdir()
        result = subprocess.run([tool, '-xpf', str(archive)], cwd=dest,
                                capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(label + ' extraction failed: ' + result.stderr)
        extracted_link = dest / link.relative_to(src)
        extracted_short = dest / short.relative_to(src)
        extracted_target = dest / target.relative_to(src)
        assert os.readlink(extracted_link) == long_link
        assert os.readlink(extracted_short) == link.name
        assert extracted_link.read_bytes() == target.read_bytes()
        assert extracted_short.read_bytes() == target.read_bytes()
        assert stat.S_IMODE(extracted_target.stat().st_mode) == 0o640
        print(label + ' PASS: long/short link text, resolution, bytes, mode')
    print('baseline ustar rejection PASS; link target bytes:', len(os.fsencode(long_link)))

    # Exercise the same METALOG -> mtree -> pax -M path as maketars.
    directories = {src}
    for obj in (target, link, short):
        parent = obj.parent
        while parent != src:
            directories.add(parent)
            parent = parent.parent
    spec = '/set uname=root gname=wheel\n. type=dir mode=0755\n' + ''.join(
        './' + str(d.relative_to(src)) + ' type=dir mode=0755\n'
        for d in sorted(directories - {src}, key=lambda x: (len(x.parts), str(x))))
    spec += ''.join('./' + str(p.relative_to(src)) +
        (' type=link mode=0777 link=' + os.readlink(p) if p.is_symlink() else ' type=file mode=0640') + '\n'
        for p in (target, link, short))
    mtree = subprocess.run([str(a.mtree.resolve()), '-C', '-k', 'all', '-N', str(a.etc.resolve())],
                           input=spec, capture_output=True, text=True, check=True)
    stamped = []
    for i in range(2):
        packed = root / ('metadata-' + str(i) + '.tar')
        r = subprocess.run([pax, '--gnu', '-dOw', '--timestamp', '1793296914',
                            '-N', str(a.etc.resolve()), '-M', '-f', str(packed)],
                           cwd=src, input=mtree.stdout, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit('mtree pax write failed: ' + r.stderr)
        stamped.append(packed)
    assert stamped[0].read_bytes() == stamped[1].read_bytes(), 'non-reproducible archive'
    with tarfile.open(stamped[0]) as tf:
        members = {m.name.removeprefix('./'): m for m in tf.getmembers()}
        for obj in (target, link, short):
            m = members[str(obj.relative_to(src))]
            assert (m.uid, m.gid, m.uname, m.gname) == (0, 0, 'root', 'wheel')
            assert stat.S_IMODE(m.mode) == (0o777 if obj.is_symlink() else 0o640), (str(obj), oct(m.mode))
        assert members[str(link.relative_to(src))].linkname == long_link
        assert members[str(short.relative_to(src))].linkname == link.name
    for label, tool in [('native pax tar-mode', str(tar_alias)), ('host bsdtar', host_tar)]:
        dest = root / ('metadata-extract-' + label.replace(' ', '-'))
        dest.mkdir()
        subprocess.run([tool, '-xpf', str(stamped[0])], cwd=dest, check=True,
                       capture_output=True, text=True)
        assert os.readlink(dest / link.relative_to(src)) == long_link
        assert os.readlink(dest / short.relative_to(src)) == link.name
        assert (dest / link.relative_to(src)).read_bytes() == target.read_bytes()
        assert stat.S_IMODE((dest / target.relative_to(src)).stat().st_mode) == 0o640
    print('mtree -M user/group/mode and repeated timestamp archive bytes PASS')
