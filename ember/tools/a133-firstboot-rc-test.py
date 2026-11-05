#!/usr/bin/env python3
# Origin: EmberBSD - execute the optional firstboot rc body with safe service boundaries.
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / 'ember/boot/a133-firstboot-guard.rc'
assert source.is_file(), 'firstboot rc owner is missing'
with tempfile.TemporaryDirectory(prefix='a133-firstboot-rc-') as directory:
    base = Path(directory)
    marker, effects = base / 'pending', base / 'effects'
    library = base / 'guard.sh'
    library.write_text((root / 'ember/boot/a133-firstboot-guard.sh').read_text() + '''
a133_firstboot_wait() {
    printf 'wait:%s\\n' "$1" >> "$EFFECTS"
    [ "$SCENARIO" != cancel ] || rm "$MARKER"
}
a133_firstboot_recover() { printf 'recover\\n' >> "$EFFECTS"; }
''')
    rcsubr = base / 'rc.subr'
    rcsubr.write_text('''load_rc_config() { :; }
run_rc_command() {
    [ "$ember_firstboot_guard" = YES ] || return 0
    "$start_cmd" || return
    wait
}
''')
    mcu = base / 'mcu'
    mcu.write_text('#!/bin/sh\n[ "$1" = status ] && [ "$SCENARIO" != mcu_failed ]\n')
    mcu.chmod(0o700)
    text = source.read_text().replace('/etc/rc.subr', str(rcsubr))
    text = text.replace('/usr/libexec/a133-firstboot-guard.sh', str(library))
    text = text.replace('/etc/rc.d/mcu7502_keepalive', str(mcu))
    text = text.replace('/etc/ember/firstboot-pending', str(marker))
    text = text.replace('/var/log/firstboot-guard.log', str(base / 'guard.log'))
    script = base / 'rc'
    script.write_text(text)
    for scenario in ('pending', 'cancel', 'missing', 'symlink', 'disabled', 'mcu_failed', 'missing_helper'):
        if marker.exists() or marker.is_symlink():
            marker.unlink()
        if effects.exists():
            effects.unlink()
        if scenario != 'missing':
            marker.write_text('4' * 48 + '\n')
            marker.chmod(0o600)
        if scenario == 'symlink':
            marker.unlink()
            (base / 'foreign').write_text('another regular file')
            marker.symlink_to(base / 'foreign')
        if scenario == 'missing_helper':
            library.rename(base / 'hidden-guard')
        env = dict(os.environ, SCENARIO=scenario, MARKER=str(marker), EFFECTS=str(effects),
                   ember_firstboot_guard='NO' if scenario == 'disabled' else 'YES', _rc_conf_loaded='true')
        result = subprocess.run(['/bin/sh', str(script), 'start'], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        expected = ['wait:120', 'recover'] if scenario == 'pending' else ['wait:120'] if scenario == 'cancel' else []
        actual = effects.read_text().splitlines() if effects.exists() else []
        assert actual == expected, (scenario, actual)
        assert (result.returncode != 0) == (scenario in ('mcu_failed', 'missing_helper')), (scenario, result.returncode)
        assert not result.stderr, (scenario, result.stderr)
print('Firstboot rc: inherited enable, pending/cancel, disabled/missing/symlink, MCU and library failure passed')
