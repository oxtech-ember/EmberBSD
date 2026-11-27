# SPDX-License-Identifier: BSD-2-Clause
"""Read a local factory input with a fixed size limit."""

import os
import stat


def read_regular(path, limit, *, exact=False):
    if not stat.S_ISREG(path.lstat().st_mode):
        raise ValueError(f"{path.name}: expected a regular file")
    flags = os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW
    with os.fdopen(os.open(path, flags), "rb") as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise ValueError(f"{path.name}: expected a regular file")
        if exact and info.st_size != limit:
            raise ValueError(f"{path.name}: expected exactly {limit} bytes")
        if not 0 < info.st_size <= limit:
            raise ValueError(f"{path.name}: unexpected file size")
        data = source.read(info.st_size + 1)
        if len(data) != info.st_size:
            raise ValueError(f"{path.name}: file size changed")
    return data
