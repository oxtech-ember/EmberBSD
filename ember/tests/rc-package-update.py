#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD contributors (AI-assisted).
"""Exercise the real rc.subr and the updater's process lifetime in a private directory."""

from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile
import time


def wait_for(predicate):
    deadline = time.monotonic() + 5
    while not predicate():
        if time.monotonic() >= deadline:
            raise AssertionError("timed out waiting for fixture")
        time.sleep(0.05)


def lifecycle(directory):
    src = Path(__file__).resolve().parents[2]
    name = "ember_pkg_update"
    rc_conf = directory / "rc.conf"
    rc_conf.write_text(f". {shlex.quote(str(src / 'etc/defaults/rc.conf'))}\n")
    rc_subr = directory / "rc.subr"
    rc_subr.write_text((src / "etc/rc.subr").read_text().replace("/etc/rc.conf", str(rc_conf)))
    pidfile = directory / f"{name}.pid"
    client = directory / "ember-sdk.pyz"
    config = directory / "config"
    config.touch()
    log = directory / "messages"
    logger = directory / "logger"
    logger.write_text(f'#!/bin/sh\n[ "$*" = "-p daemon.info -t {name}" ] || exit 2\n'
                      f'exec cat >> {shlex.quote(str(log))}\n')
    logger.chmod(0o755)
    client.write_text("""\
import os, signal, sys
assert sys.argv[1:] == ["--config", sys.argv[2], "auto-update"]
assert os.path.isfile(sys.argv[2]) and not sys.stdin.read() and os.getcwd() == "/"
def stop(*_args):
    print("stopped", flush=True)
    sys.exit(0)
signal.signal(signal.SIGTERM, stop)
print("ready", flush=True)
print("diagnostic", file=sys.stderr, flush=True)
while True:
    signal.pause()
""")
    script = (src / "etc/rc.d" / name).read_text()
    for original, target in (("/etc/rc.subr", rc_subr), ("/var/run/${name}.pid", pidfile),
                             ("/usr/pkg/bin/python3.15", sys.executable),
                             ("/usr/local/libexec/ember-sdk.pyz", client),
                             ("/usr/local/etc/ember-sdk.json", config), ("/usr/bin/logger", logger)):
        script = script.replace(original, str(target))
    service = directory / name
    service.write_text(script)
    # macOS /bin/sh is Bash 3.2 and cannot parse an unrelated ZFS function
    # in NetBSD rc.subr. The system dash accepts this POSIX shell syntax.
    shell = "/bin/dash" if sys.platform == "darwin" else "/bin/sh"

    def rc(action):
        return subprocess.run([shell, service, action], capture_output=True, text=True, timeout=8)

    def messages():
        return log.read_text().splitlines() if log.exists() else []

    assert f"{name}=NO" in rc("rcvar").stdout
    assert rc("faststart").returncode and not pidfile.exists()
    service_conf = directory / "rc.conf.d"
    service_conf.mkdir()
    (service_conf / name).write_text(f"{name}=YES\n")
    with ThreadPoolExecutor(max_workers=4) as pool:
        starts = list(pool.map(rc, ["faststart", "start", "start", "start"]))
    try:
        assert sum(r.returncode == 0 for r in starts) == 1, starts
        wait_for(lambda: "diagnostic" in messages())
        pid = int(pidfile.read_text())
        assert str(pid) in rc("status").stdout and messages().count("ready") == 1
        assert rc("stop").returncode == 0 and rc("status").returncode != 0
        wait_for(lambda: "stopped" in messages())

        # A stale pidfile must not authorize signalling even an unrelated
        # foreground process running the exact same SDK command.
        with subprocess.Popen([sys.executable, "-u", client, "--config", config, "auto-update"],
                              stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, cwd="/") as other:
            try:
                pidfile.write_text(f"{other.pid}\n")
                assert rc("status").returncode and rc("stop").returncode
                assert other.poll() is None
            finally:
                other.terminate()
        assert rc("start").returncode == 0
        wait_for(lambda: messages().count("ready") == 2)
        os.kill(int(pidfile.read_text()), signal.SIGKILL)
        wait_for(lambda: rc("status").returncode != 0)
        assert rc("start").returncode == 0
        wait_for(lambda: messages().count("ready") == 3)
    finally:
        rc("stop")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as temporary:
        lifecycle(Path(temporary))
    print("package updater rc lifecycle passed")
