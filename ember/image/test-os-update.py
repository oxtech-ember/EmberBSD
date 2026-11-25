#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise the installed OS service while the loader consumes each trial once."""

import contextlib
import hashlib
import importlib.machinery
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
service_path = ROOT / 'libexec/ember-os-update/ember-os-update'
loader = importlib.machinery.SourceFileLoader('service', str(service_path))
service = importlib.util.module_from_spec(importlib.util.spec_from_loader('service', loader))
loader.exec_module(service)


class ServiceTests(unittest.TestCase):
    def setUp(self):
        temp = self.enterContext(tempfile.TemporaryDirectory())
        self.roots = {slot: Path(temp) / slot for slot in ('A', 'B')}
        for root in self.roots.values():
            root.mkdir()
        self.status = dict(running='A', confirmed='A', next=None, trial=False, prepared=None)
        self.config = dict(profile='machine-profile', backend='/slot', health='/health')
        self.stop = threading.Event()
        self.events = []
        self.bad = None
        self.run_dir = Path(temp) / 'run'
        self.run_dir.mkdir()
        self.enterContext(patch.object(service, 'PIDFILE', self.run_dir / 'ember_os_update.pid'))
        self.enterContext(patch.object(service, 'trusted_file', side_effect=lambda path: path))
        self.wall_clock = 1
        self.stop_at = None
        self.active_kernel = '1' * 40
        self.latest = self.release(2)
        service.save(self.roots['A'] / 'release.json', self.release(1))
        self.enterContext(patch.object(service, 'run', side_effect=self.run_command))
        self.enterContext(patch.object(service, 'package_lock', side_effect=self.lock))
        self.enterContext(patch.object(service, 'fetch', side_effect=self.fetch))
        self.enterContext(patch.object(service, 'restart', side_effect=self.restart))
        self.enterContext(patch.object(service.subprocess, 'run', side_effect=self.health))
        self.locked = False

    def release(self, generation):
        return dict(profile=self.config['profile'], generation=generation,
                    os_commit=str(generation) * 40)

    @contextlib.contextmanager
    def lock(self):
        self.assertFalse(self.locked)
        self.locked = True
        try:
            yield
        finally:
            self.locked = False

    def run_command(self, program, cmd, *args):
        if program == '/sbin/sysctl':
            if args == ('kern.boottime',):
                return str(self.wall_clock)
            return 'EmberBSD:' + self.active_kernel
        self.assertEqual(program, '/slot')
        if cmd == 'status':
            return json.dumps(self.status)
        self.events.append(cmd)
        if cmd == self.stop_at:
            self.stop.set()
        if cmd == self.bad:
            raise ValueError('injected ' + cmd)
        slot = args[0]
        if cmd in ('arm', 'prepare'):
            self.assertTrue(self.locked)
            self.assertNotEqual(slot, self.status['confirmed'])
        if cmd == 'prepare':
            raw = (args[1] / 'update.json').read_bytes()
            (self.roots[slot] / 'release.json').write_bytes(raw)
            self.status['prepared'] = dict(slot=slot, sha256=hashlib.sha256(raw).hexdigest())
            if self.bad == 'wrong-hash':
                self.status['prepared']['sha256'] = '0' * 64
            (self.roots[slot] / 'attempt.json').unlink(missing_ok=True)
        elif cmd == 'arm':
            self.status['next'] = slot
        elif cmd == 'cancel':
            self.assertEqual(slot, self.status['next'])
            self.status['next'] = None
        elif cmd == 'confirm':
            self.status['confirmed'] = slot
        else:
            self.fail(cmd)

    def fetch(self, profile, floor, directory):
        self.assertTrue(self.locked)
        self.events.append(('fetch', floor))
        if self.stop_at == 'fetch':
            self.stop.set()
        if self.bad == 'fetch':
            raise ValueError('invalid signature or interrupted download')
        if self.latest['generation'] <= floor:
            return None
        directory.mkdir()
        service.save(directory / 'update.json', self.latest)
        return self.latest

    def restart(self, stopping):
        self.assertTrue(self.locked)
        self.events.append('reboot')
        if self.bad == 'reboot':
            raise OSError('restart interrupted')
        self.boot()

    def boot(self):
        (self.run_dir / 'ember-os-update.boot').unlink(missing_ok=True)
        target = self.status['next']
        slot = target or self.status['confirmed']
        self.status.update(running=slot, next=None, trial=target is not None)
        self.active_kernel = service.read(self.roots[slot] / 'release.json')['os_commit']

    def tick(self):
        with patch.object(service, 'STATE', self.roots[self.status['running']]):
            service.cycle(self.config, self.stop)

    def health(self, argv, **kw):
        self.assertEqual(argv, ['/health'])
        self.events.append('health')
        if self.stop_at == 'health':
            self.stop.set()
        if self.bad == 'health':
            raise subprocess.CalledProcessError(1, argv)

    def test_update_confirm_and_later_update_in_reverse(self):
        self.tick()
        self.tick()
        self.assertEqual(self.events, [('fetch', 1), 'prepare', 'arm', 'reboot', 'health', 'confirm'])
        self.assertEqual(self.status['confirmed'], 'B')
        self.boot()
        self.tick()
        self.assertEqual(self.events[-1], ('fetch', 2))
        self.latest = self.release(3)
        self.tick()
        self.tick()
        self.assertEqual(self.status['confirmed'], 'A')

    def test_reset_before_userspace_rejects_generation_and_accepts_newer(self):
        self.tick()
        self.boot()
        self.tick()
        self.assertEqual(self.events[-1], ('fetch', 2))
        self.assertEqual(self.status['confirmed'], 'A')
        self.assertEqual(self.events.count('arm'), 1)
        self.latest = self.release(3)
        self.tick()
        self.tick()
        self.assertEqual(self.status['confirmed'], 'B')

    def test_health_failure_keeps_the_confirmed_root(self):
        self.tick()
        self.bad = 'health'
        with self.assertRaises(subprocess.CalledProcessError):
            self.tick()
        self.assertNotIn('confirm', self.events)
        self.boot()
        self.assertEqual(self.status['running'], 'A')

    def test_wrong_running_revision_does_not_confirm_or_download(self):
        self.tick()
        self.active_kernel = 'f' * 40
        before = list(self.events)
        with self.assertRaisesRegex(ValueError, 'running kernel'):
            self.tick()
        self.assertEqual(self.events, before)

    def test_interrupted_download_preparation_or_unarmed_write_can_retry(self):
        for stage in ('fetch', 'prepare', 'arm'):
            with self.subTest(stage=stage):
                self.bad = stage
                with self.assertRaises(ValueError):
                    self.tick()
                self.assertIsNone(self.status['next'])
                self.assertFalse((self.roots['A'] / 'attempt.json').exists())
                self.assertFalse((self.roots['A'] / 'incoming').exists())
        self.bad = None
        self.tick()
        self.assertEqual(self.events[-1], 'reboot')

    def test_interrupted_restart_resumes_only_the_armed_trial(self):
        self.bad = 'reboot'
        with self.assertRaises(OSError):
            self.tick()
        self.bad = None
        self.tick()
        self.assertEqual(self.events.count('arm'), 1)
        self.assertEqual(self.events.count('reboot'), 2)
        self.tick()
        self.assertEqual(self.status['confirmed'], 'B')

    def test_foreign_or_unrecognized_state_stops_before_boot_writes(self):
        self.status['next'] = 'B'
        with self.assertRaisesRegex(ValueError, 'unrecognized pending'):
            self.tick()
        self.status.update(next=None, running='B')
        service.save(self.roots['B'] / 'release.json', self.release(1))
        with self.assertRaisesRegex(ValueError, 'unrecognized unconfirmed'):
            self.tick()
        self.status['running'] = 'A'
        self.config['profile'] = 'other'
        with self.assertRaisesRegex(ValueError, 'installed OS receipt'):
            self.tick()
        self.assertEqual(self.events, [])

    def test_stop_during_fetch_does_not_prepare(self):
        self.stop_at = 'fetch'
        self.tick()
        self.assertEqual(self.events, [('fetch', 1)])
        self.assertIsNone(self.status['next'])

    def test_stop_during_preparation_keeps_previous_boot_default(self):
        self.stop_at = 'prepare'
        self.tick()
        self.assertEqual(self.events, [('fetch', 1), 'prepare'])
        self.assertIsNone(self.status['next'])

    def test_stop_during_health_keeps_watchdog_and_previous_slot(self):
        self.tick()
        self.stop_at = 'health'
        self.tick()
        self.assertEqual(self.events[-1], 'health')
        self.assertEqual(self.status['confirmed'], 'A')

    def test_stop_during_arm_defers_restart_and_validates_pending_on_resume(self):
        self.stop_at = 'arm'
        self.tick()
        self.assertEqual(self.events[-1], 'arm')
        self.assertEqual(self.status['next'], 'B')
        attempt = service.read(self.roots['A'] / 'attempt.json')
        self.assertEqual(attempt['sha256'], self.status['prepared']['sha256'])
        self.stop.clear()
        self.wall_clock += 3600
        self.tick()
        self.assertEqual(self.events[-1], 'reboot')

    def test_unconsumed_marker_on_next_boot_is_cancelled_without_reboot(self):
        self.stop_at = 'arm'
        self.tick()
        self.stop.clear()
        (self.run_dir / 'ember-os-update.boot').unlink(missing_ok=True)
        self.bad = 'cancel'
        with self.assertRaises(ValueError):
            self.tick()
        self.assertEqual(self.status['running'], 'A')
        self.bad = None
        self.tick()
        self.assertIsNone(self.status['next'])
        self.tick()
        self.assertEqual(self.events[-1], ('fetch', 2))
        self.assertNotIn('reboot', self.events)

    def test_pending_attempt_for_different_payload_refuses_restart(self):
        self.stop_at = 'arm'
        self.tick()
        self.stop.clear()
        self.status['prepared']['sha256'] = 'e' * 64
        with self.assertRaisesRegex(ValueError, 'pending boot differs'):
            self.tick()
        self.assertNotIn('reboot', self.events)

    def test_backend_must_prepare_the_exact_authenticated_descriptor(self):
        self.bad = 'wrong-hash'
        with self.assertRaisesRegex(ValueError, 'prepare this OS release'):
            self.tick()
        self.assertNotIn('arm', self.events)
        self.assertFalse((self.roots['A'] / 'attempt.json').exists())

    def test_same_kernel_with_foreign_receipt_bytes_cannot_confirm(self):
        self.latest['os_commit'] = self.active_kernel
        self.tick()
        path = self.roots['B'] / 'release.json'
        for raw in ((self.roots['A'] / 'release.json').read_bytes(), path.read_bytes() + b' '):
            path.write_bytes(raw)
            with self.assertRaisesRegex(ValueError, 'prepared OS identity'):
                self.tick()
        self.assertNotIn('health', self.events)
        self.assertNotIn('confirm', self.events)

    def test_bootstrap_generation_is_never_a_trial_even_with_matching_digest(self):
        self.tick()
        path = self.roots['B'] / 'release.json'
        service.save(path, dict(self.latest, generation=0))
        self.status['prepared']['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, 'prepared OS identity'):
            self.tick()
        self.assertNotIn('confirm', self.events)


if __name__ == '__main__':
    unittest.main()
