#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; selected external U02 instruction fixture entry point.
import json
import os
from pathlib import Path
import signal
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent / 'aicwf-firmware-check'))

if __name__ == '__main__':
    def interrupted(number, frame):
        raise SystemExit(128 + number)
    for number in (signal.SIGINT, signal.SIGHUP, signal.SIGTERM):
        signal.signal(number, interrupted)
    previous = os.umask(0o077)
    try:
        from driver import main
        print(json.dumps(main(sys.argv[1:]), sort_keys=True))
    except Exception as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(2)
    finally:
        os.umask(previous)
