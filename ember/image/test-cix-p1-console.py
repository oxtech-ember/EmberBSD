#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the trusted console wrapper in an owned controlling PTY."""

if not __debug__:
    raise SystemExit('CIX contract tests require Python assertions')
import errno
import os
from pathlib import Path
import pty
import re
import select
import shlex
import signal
import subprocess
import tempfile
import time

shell = os.environ.get('TEST_SH')
if not shell or not os.path.isabs(shell):
    raise SystemExit('Set TEST_SH to the selected native shell')
wrapper = Path(__file__).with_name('cix-p1-console').read_text()
checks = 0
probe_env = dict(os.environ, ENV='', BASH_ENV='')
probe_env.pop('BASH_VERSION', None)
shell_probe = subprocess.run(
    [shell, '-c', 'printf "%s" "${BASH_VERSION-}"'],
    env=probe_env,
    check=True, capture_output=True, text=True)
bash_startup_options = ' --noprofile --norc' if shell_probe.stdout else ''


def check(condition, message):
    global checks
    checks += 1
    assert condition, message


with tempfile.TemporaryDirectory(prefix='cix-console-') as tmp:
    startup_marker = Path(tmp) / 'startup-ran'
    for startup in ('.bashrc', '.bash_profile', '.profile', 'bash-env'):
        (Path(tmp) / startup).write_text(
            "PS1='unexpected-host-prompt$ '\n"
            'printf startup > ' + shlex.quote(str(startup_marker)) + '\n')
    os.environ['BASH_ENV'] = os.environ['ENV'] = str(Path(tmp) / 'bash-env')
    for boundary in ('HOME=/root', 'cd /root', 'PATH=/sbin:/bin:/usr/sbin:/usr/bin', '/bin/sh -i'):
        if wrapper.count(boundary) != 1:
            raise SystemExit('console fixture cannot isolate boundary: ' + boundary)
    candidate = wrapper.replace('HOME=/root', 'HOME=' + shlex.quote(tmp), 1)
    candidate = candidate.replace('cd /root', 'cd ' + shlex.quote(tmp), 1)
    candidate = candidate.replace('PATH=/sbin:/bin:/usr/sbin:/usr/bin',
                                  'PATH=' + shlex.quote(os.environ['PATH']), 1)
    interactive_shell = shlex.quote(shell) + bash_startup_options + ' -i'
    candidate = candidate.replace('/bin/sh -i', interactive_shell)
    candidate_path = Path(tmp) / 'console'
    candidate_path.write_text(candidate)

    def run(path, scenario):
        pid, fd = pty.fork()
        if pid == 0:
            env = dict(os.environ, ENV='', BASH_ENV='', TERM='dumb')
            os.execve(shell, [shell, str(path)], env)
        child = command = None
        output = bytearray()
        eof = reaped = False
        status = None

        def drain(seconds):
            nonlocal eof
            if not eof and select.select([fd], [], [], seconds)[0]:
                try:
                    data = os.read(fd, 4096)
                    if data:
                        output.extend(data)
                    else:
                        eof = True
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    eof = True

        def marker(name):
            deadline = time.monotonic() + 3
            pattern = re.compile(name.encode() + rb':(\d+)\r?\n')
            while True:
                match = pattern.search(output)
                if match:
                    return int(match.group(1))
                if eof or time.monotonic() >= deadline:
                    raise RuntimeError(f'{name} timeout: {bytes(output)!r}')
                drain(0.02)

        try:
            os.write(fd, b'printf \'CIX_SHELL:%s\\n\' "$$"\n')
            child = marker('CIX_SHELL')
            check(os.getsid(pid) == pid and os.getsid(child) == pid,
                  'wrapper and interactive shell share owned controlling session')
            if scenario == 'baseline':
                check(child == pid, 'exec wrapper replaces its session leader with the interactive shell')
            else:
                check(child != pid and os.getpgid(child) == child, 'interactive child has normal job control')
            if scenario == 'normal':
                deadline = time.monotonic() + 3
                while not output.endswith(b'ember-cix# '):
                    if eof or time.monotonic() >= deadline:
                        raise RuntimeError(f'initial prompt timeout: {bytes(output)!r}')
                    drain(0.02)

                def interrupt():
                    offset = len(output)
                    os.write(fd, b'\x03')
                    deadline = time.monotonic() + 3
                    while b'ember-cix# ' not in output[offset:]:
                        if eof or time.monotonic() >= deadline:
                            raise RuntimeError(f'interrupt prompt timeout: {bytes(output)!r}')
                        drain(0.02)

                interrupt()
                os.write(fd, b'printf \'CIX_IDLE:%s\\n\' "$$"\n')
                check(marker('CIX_IDLE') == child, 'Ctrl-C at idle keeps the interactive shell')
                body = 'printf \'CIX_COMMAND:%s\\n\' "$$"; exec sleep 30'
                os.write(fd, (shlex.quote(shell) + ' -c ' + shlex.quote(body) + '\n').encode())
                command = marker('CIX_COMMAND')
                check(os.getsid(command) == pid and os.getpgid(command) != os.getpgid(child),
                      'foreground command has its own job process group')
                interrupt()
                os.write(fd, b'printf \'CIX_AFTER_INT:%s\\n\' "$$"\n')
                check(marker('CIX_AFTER_INT') == child, 'Ctrl-C returns to same prompt')
                command = None
                os.write(fd, b'exit 7\n')
            else:
                os.kill(pid, signal.SIGTERM)
            deadline = time.monotonic() + (0.25 if scenario == 'baseline' else 3)
            while True:
                if not reaped:
                    result, status = os.waitpid(pid, os.WNOHANG)
                    reaped = result != 0
                drain(0.01)
                if (reaped and eof) or time.monotonic() >= deadline:
                    break
            diagnostic = f'status={status!r}, PTY_EOF={eof}, output={bytes(output)!r}'
            if scenario == 'baseline':
                check(not reaped and not eof, 'idle exec baseline survives TERM: ' + diagnostic)
            elif scenario == 'normal':
                check(reaped and os.WIFEXITED(status) and os.WEXITSTATUS(status) == 7 and eof,
                      'wrapper preserves child status: ' + diagnostic)
            else:
                check(reaped and os.WIFSIGNALED(status) and os.WTERMSIG(status) == signal.SIGTERM and eof,
                      'leader TERM closes idle console: ' + diagnostic)
            if eof:
                child = None
        finally:
            # Signal only tracked children, never a process group.
            for owned in {x for x in (command, child) if x is not None and x != pid}:
                try:
                    os.kill(owned, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if not reaped:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            os.close(fd)
            if not reaped:
                os.waitpid(pid, 0)

    run(candidate_path, 'term')
    run(candidate_path, 'normal')
    baseline = re.sub(r'^(' + re.escape(interactive_shell) + r')\n.*\Z', r'exec \1\n', candidate,
                      flags=re.MULTILINE | re.DOTALL)
    baseline_path = Path(tmp) / 'console-exec'
    baseline_path.write_text(baseline)
    run(baseline_path, 'baseline')
    check(not startup_marker.exists(), 'host shell startup files were not executed')

print(f'CIX console PTY lifecycle: {checks} checks, 0 failures')
