#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
"""Exercise the Wi-Fi roam target script with mock system commands."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--shell', required=True,
                    help='selected native host shell (run in POSIX mode)')
args = parser.parse_args()
shell = Path(args.shell).resolve(strict=True)
source = Path(__file__).resolve().parents[1]
script = source / 'tools/wifi-roam-check.sh'
ap_a = '02:00:00:00:00:0a'
ap_b = '02:00:00:00:00:0b'
ssid = r' Office\tNetwork '


def check(condition, message):
    if not condition:
        raise SystemExit('FAIL: ' + message)


def run_case(mode, expected, expected_roams, limit=80, timestamp=False):
    with tempfile.TemporaryDirectory(prefix='wifi-roam-check-') as temporary:
        root = Path(temporary)
        bindir = root / 'bin'
        bindir.mkdir()
        (root / 'clock').write_text('1000\n')
        (root / 'result-count').write_text('0\n')
        (root / 'roam-count').write_text('0\n')
        (root / 'current-bssid').write_text(ap_a + '\n')

        def command(name, body):
            path = bindir / name
            path.write_text('#!' + str(shell) + '\nset -o posix\n' + body)
            path.chmod(0o755)

        command('date', 'cat "$TEST_ROOT/clock"\n')
        command('sleep',
                'n=${1:-1}\n'
                'case "$TEST_MODE" in no-candidate) n=25 ;; esac\n'
                'now=$(cat "$TEST_ROOT/clock")\n'
                'echo "$((now + n))" > "$TEST_ROOT/clock"\n')
        wpa = f'''op=$3
arg=$4
if [ "$TEST_TIMESTAMP" = 1 ]; then
    printf '12:02:56.490: '
fi
case "$op" in
status)
    bssid=$(cat "$TEST_ROOT/current-bssid")
    printf 'bssid=%s\\nfreq=2412\\nssid=%s\\nwpa_state=COMPLETED\\n' \\
        "$bssid" '{ssid}'
    if [ "{mode}" = pass-psk ]; then
        printf 'key_mgmt=WPA2-PSK\\nip_address=192.0.2.10\\n'
    else
        printf 'key_mgmt=SAE\\nip_address=192.0.2.10\\n'
    fi
    ;;
bss_flush)
    if [ "{mode}" = control-fail ]; then
        echo FAIL
    else
        echo OK
    fi
    ;;
scan_results)
    count=$(cat "$TEST_ROOT/result-count")
    echo "$((count + 1))" > "$TEST_ROOT/result-count"
    printf 'bssid / frequency / signal level / flags / ssid\\n'
    printf '{ap_a}\\t2412\\t-40\\t[WPA2-PSK-SAE-CCMP][ESS]\\t%s\\n' '{ssid}'
    if [ "{mode}" != no-candidate ] && [ "{mode}" != global-deadline ]; then
        printf '{ap_b}\\t2437\\t-50\\t[WPA2-PSK-SAE-CCMP][ESS]\\t%s\\n' '{ssid}'
    fi
    if [ "{mode}" = switch-after-results ] && [ "$count" -eq 1 ]; then
        echo {ap_a} > "$TEST_ROOT/current-bssid"
    fi
    ;;
scan)
    if [ "{mode}" = switch-after-results ]; then
        echo {ap_b} > "$TEST_ROOT/current-bssid"
    fi
    echo OK
    ;;
bss)
    printf 'bssid=%s\\nssid=%s\\nage=0\\n' "$arg" '{ssid}'
    ;;
roam)
    count=$(cat "$TEST_ROOT/roam-count")
    echo "$((count + 1))" > "$TEST_ROOT/roam-count"
    if [ "{mode}" = roam-fail ]; then
        echo FAIL
    elif [ "{mode}" = roam-no-transition ]; then
        echo OK
    else
        echo "$arg" > "$TEST_ROOT/current-bssid"
        echo OK
    fi
    ;;
*)
    echo FAIL
    ;;
esac
'''
        command('wpa_cli', wpa)
        command('getent', 'echo "192.0.2.53 $2"\n')
        command('route', 'echo "gateway: 192.0.2.1"\n')
        command('ping', 'exit 0\n')

        logs = root / 'logs'
        env = os.environ.copy()
        env.update(TEST_MODE=mode, TEST_ROOT=str(root),
                   TEST_TIMESTAMP='1' if timestamp else '0',
                   PATH=str(bindir) + ':' + env['PATH'])
        proc = subprocess.run([str(shell), '--posix', str(script), 'wlan0',
                               str(logs), str(limit), 'example.test'],
                              env=env, text=True, capture_output=True,
                              timeout=15)
        result_path = logs / 'result'
        check(result_path.is_file(), mode + ': result record missing')
        result = result_path.read_text().splitlines()
        check(proc.returncode == expected[0] and
              result == ['result=' + expected[1], 'reason=' + expected[2]],
              mode + ': unexpected outcome: ' + repr((proc.returncode, result,
                                                       proc.stdout, proc.stderr)))
        check(logs.stat().st_mode & 0o777 == 0o700 and
              result_path.stat().st_mode & 0o777 == 0o600,
              mode + ': private log permissions were not retained')
        check(int((root / 'roam-count').read_text()) == expected_roams,
              mode + ': unexpected number of ROAM requests')
        if timestamp:
            check((logs / 'status.raw').read_text().startswith('12:02:56.490: '),
                  mode + ': original timestamped reply was not retained')
            check((logs / 'status').read_text().startswith('bssid='),
                  mode + ': timestamp was not removed from the first field')
        if mode == 'switch-after-results':
            check((logs / 'requested-bssid').read_text().strip() == ap_b,
                  'automatic move after scan results must be rechecked before ROAM')


run_case('pass', (0, 'PASS', 'roam_and_connectivity'), expected_roams=1)
run_case('pass-psk', (0, 'PASS', 'roam_and_connectivity'), expected_roams=1)
run_case('control-fail', (1, 'FAIL', 'cache_flush'), expected_roams=0)
run_case('no-candidate', (77, 'SKIP', 'no_fresh_compatible_candidate'),
         expected_roams=0)
run_case('switch-after-results', (0, 'PASS', 'roam_and_connectivity'),
         expected_roams=1)
run_case('global-deadline', (1, 'FAIL', 'deadline'), expected_roams=0, limit=1)
run_case('roam-fail', (1, 'FAIL', 'roam_control'), expected_roams=1)
run_case('roam-no-transition', (1, 'FAIL', 'roam_not_completed'),
         expected_roams=1, limit=1)
run_case('pass', (0, 'PASS', 'roam_and_connectivity'), expected_roams=1,
         timestamp=True)
run_case('control-fail', (1, 'FAIL', 'cache_flush'), expected_roams=0,
         timestamp=True)
run_case('roam-fail', (1, 'FAIL', 'roam_control'), expected_roams=1,
         timestamp=True)
print('wifi roam check mock scenarios: PASS')
