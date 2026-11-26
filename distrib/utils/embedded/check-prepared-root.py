#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Find the init payload in a prepared root and its flattened mtree."""

from collections import deque
import os
from pathlib import Path
import sys


def init_payload(root, nodes):
    parts = []
    pending = deque(("sbin", "init"))
    links = 0
    while pending:
        part = pending.popleft()
        if part in ("", "."):
            continue
        if part == "..":
            if parts:
                parts.pop()
            continue
        path = root.joinpath(*parts, part)
        entry = nodes.get("./" + "/".join([*parts, part]), {})
        if path.is_symlink():
            target = os.readlink(path)
            if entry.get("type") != "link" or entry.get("link") != target:
                raise ValueError("an init link differs from the root specification")
            links += 1
            if links > 32:  # NetBSD MAXSYMLINKS in sys/sys/param.h
                raise ValueError("too many symbolic links in the init path")
            if target.startswith("/"):
                parts.clear()
            pending.extendleft(reversed(target.split("/")))
        elif pending:
            if entry.get("type") != "dir" or not path.is_dir():
                raise ValueError("the root specification must include each init directory")
            parts.append(part)
        elif entry.get("type") == "file" and path.is_file() and int(entry.get("mode", "0"), 8) & 0o111:
            return path
        else:
            break
    raise ValueError("the root specification must include an executable /sbin/init payload")


def check(root, spec, console_device):
    nodes = {}
    for line in spec.read_text().splitlines():
        if line:
            path, *fields = line.split()
            nodes[path] = dict(field.split("=", 1) for field in fields)
    console = nodes.get("./dev/console", {})
    if console.get("type") != "char" or int(console.get("device", "-1"), 0) != console_device:
        raise ValueError("the root specification must provide the target /dev/console")
    return init_payload(root.resolve(strict=True), nodes)


if __name__ == "__main__":
    try:
        print(check(Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3], 0)))
    except (OSError, ValueError, RuntimeError, IndexError) as exc:
        sys.exit(f"Prepared root: {exc}")
