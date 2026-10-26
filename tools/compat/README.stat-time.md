# Shared stat timestamps

[compat_stat_time.h](compat_stat_time.h) returns complete native timestamps.
It exposes atime, mtime and ctime through three inline accessors.
Normal configuration selects st_atim or NetBSD's st_atimensec family.
Native NetBSD uses its own member family. Other native headers keep the existing
st_atimespec family. No feature result or timestamp is fabricated.

The normal compatibility library installs this header with its public headers.
Finite source contracts list the exact header and retain its source proof.
Each consumer must compile a typed native control and verify nonzero nanoseconds.
The [source test](test-stat-time.py) checks all selectors and normal installation.
It does not replace compilation or runtime checks on other hosts.
