#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; host protocol and admission controls, not ARM execution.
import ast
import inspect
import textwrap
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace
import runpy
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import irq
import driver
import patched
import patching
import policy
from remote import Remote


class ProtocolDouble:
    def __init__(self):
        self.commands = []; self.pc = 0x100; self.words = {}; self.frame_pc = 0x142d10
        self.lr = 0xfffffff9; self.thumb = 0x01000000
    def register(self, n):
        return {13: 0x20010000, 14: self.lr, 15: self.pc}[n]
    def memory(self, address, count):
        if address == 0x20010000:
            return struct.pack('<8I', 0, 0, 0, 0, 0, 0, self.frame_pc, self.thumb)
        return self.words.get(address, bytes(count))
    def command(self, text):
        self.commands.append(text); return b'T05'
    def breakpoint(self, address):
        self.commands.append(('add', address))
    def remove_breakpoint(self, address):
        self.commands.append(('remove', address))


class IRQControls(unittest.TestCase):
    def observer(self):
        a = {name: 0x20000000 + index * 0x100 for index, name in enumerate(sorted(irq.SYMBOLS))}
        a['harness_done'] = 0x800; a['results'] = 0x20010000
        return irq.Observer(ProtocolDouble(), a, patched.inputs(irq=True))

    def test_case_complete_source_call_keeps_case_name(self):
        observer = self.observer(); observer.case = 0; row = observer.rows[0]
        tree = ast.parse(textwrap.dedent(inspect.getsource(irq.Observer.run)))
        calls = [node for node in ast.walk(tree) if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == 'event' and node.args and isinstance(node.args[0], ast.Constant) and node.args[0].value == 'case-complete']
        self.assertEqual(len(calls), 1)
        # Execute the real terminal call expression, not a mock event method.
        eval(compile(ast.Expression(body=calls[0]), '<case-complete-source>', 'eval'), {'self': observer, 'row': row})
        self.assertEqual(observer.events, [dict(case=0, event='case-complete', name=row['name'])])

    def test_protected_pc_causal_uses_actual_exception_frame(self):
        observer = self.observer(); r = observer.remote
        for pc in (0x142c10, 0x142c80, 0x142d0c, 0x165ef8, 0x165f54, 0x165f68):
            r.frame_pc = pc
            self.assertRaisesRegex(ValueError, 'inside protected', observer.exception)
        for pc in (0x142be0, 0x142d10, 0x165f60, 0x165f6c, 0x800):
            r.frame_pc = pc
            self.assertEqual(observer.exception(), pc)
        r.lr = 0xfffffffd
        self.assertRaisesRegex(ValueError, 'exception frame', observer.exception)
        r.lr = 0xfffffff9; r.thumb = 0
        self.assertRaisesRegex(ValueError, 'Thumb', observer.exception)

    def test_continue_breakpoint_and_early_irq_refusal(self):
        observer = self.observer(); r = observer.remote
        r.pc = observer.addresses['harness_irq_entry']
        r.frame_pc = 0x142c10
        self.assertRaisesRegex(ValueError, 'inside protected', observer.stop, 0x142d10)
        self.assertEqual([c for c in r.commands if isinstance(c, str)], ['c'])
        self.assertTrue(observer.events[-1]['protected'])
        observer = self.observer(); r = observer.remote; r.pc = 0x142d10
        observer.stop(0x142d10)
        self.assertNotIn(0x142d10, observer.breakpoints)
        self.assertEqual(r.commands[-2:], ['c', ('remove', 0x142d10)])

    def test_masked_pending_and_return_mask_are_both_required(self):
        observer = self.observer(); r = observer.remote
        r.words[0xe000e200] = (1 << 31).to_bytes(4, 'little')
        observer.count_pending(0, True)
        self.assertRaisesRegex(ValueError, 'count/pending', observer.count_pending, 1, False)
        at = observer.addresses['results'] + 12
        r.words[at] = (1).to_bytes(4, 'little')
        observer.returned_mask(0, 1)
        self.assertRaisesRegex(ValueError, 'PRIMASK', observer.returned_mask, 0, 0)
        r.words[0xe000e200] = bytes(4)
        self.assertRaisesRegex(ValueError, 'count/pending', observer.count_pending, 0, True)

    def test_snapshot_includes_all_context_and_both_active_pointers(self):
        observer = self.observer(); row = patched.inputs(irq=True)[0]
        state = patched.before_state(row); c = patched.context(row); v = row['vif'] * 0x528
        data = state[c:c + 120] + state[v + 0x4b0:v + 0x4b8]
        at = observer.addresses['irq_snapshot']; observer.remote.words[at] = data
        observer.snapshot(row, state)
        for offset in (0, 0x48, 0x62, 0x64, 0x74, 120, 124):
            damaged = bytearray(data); damaged[offset] ^= 1; observer.remote.words[at] = damaged
            self.assertRaisesRegex(ValueError, 'incomplete transaction', observer.snapshot, row, state)

    def test_fixed_register_and_one_nvic_bit_protocol(self):
        r = Remote.__new__(Remote); commands = []
        def answer(text):
            commands.append(text)
            return b'OK' if text.startswith('M') else b'00000080' if text.startswith('m') else b'78563412'
        r.command = answer
        self.assertEqual(r.register(13), 0x12345678)
        self.assertEqual(commands, ['pd'])
        self.assertRaises(ValueError, r.register, 16)
        r.pend_irq31()
        self.assertEqual(commands[1:], ['Me000e200,4:00000080', 'me000e200,4'])
        r.command = lambda text: b'00000000' if text.startswith('m') else b'OK'
        self.assertRaisesRegex(RuntimeError, 'pending', r.pend_irq31)

    def test_refusal_byte_evidence_requires_proven_pause_and_keeps_primary(self):
        for paused, fail_evidence in ((False, False), (True, False), (True, True)):
            with self.subTest(paused=paused, fail_evidence=fail_evidence), tempfile.TemporaryDirectory(prefix='aic-irq-refusal-') as temporary:
                root = Path(temporary); work = root / 'work'; work.mkdir()
                qemu = root / 'qemu'; qemu.write_text('not executed'); qemu.chmod(0o700)
                class Owner:
                    pid = 42
                    def __init__(self, *args, **kwargs):pass
                    def start(self):pass
                    def stop(self):pass
                class Connection:
                    def __init__(self, *args):pass
                    def breakpoint(self, *args):pass
                    def close(self):pass
                    def memory(self, address, size):return bytes(size)
                class Observer:
                    events = []
                    def __init__(self, *args):self.paused = paused
                    def run(self):raise ValueError('IRQ delivered inside protected BIP interval')
                class JSON:
                    @staticmethod
                    def write_json(path, value, **kwargs):
                        if path.name == 'irq-events.json' and fail_evidence:raise OSError('evidence failure')
                        path.write_text(json.dumps(value))
                expected = {name: hashlib.sha256(bytes(end - start)).hexdigest() for name, (start, end) in policy.SPANS.items()}
                process = SimpleNamespace(sanitized_environment=lambda: {}, OwnedProcessGroup=Owner)
                with patch.object(driver, 'Remote', Connection), patch.object(driver.irq_control, 'Observer', Observer):
                    self.assertRaisesRegex(RuntimeError if fail_evidence else ValueError, 'IRQ delivered inside protected', driver.execute, qemu, work, {'harness_done': 0x800}, 1, process, JSON, expected, {}, [], 'irq')
                self.assertEqual((work / 'loaded-after-refusal.json').exists(), paused)
                self.assertFalse((work / 'results.bin').exists())
                self.assertFalse((work / 'ownership-pending.json').exists())

    def test_finite_irq_input_and_explicit_causal_gate(self):
        observer = self.observer()
        self.assertEqual(len(observer.rows), 36)
        self.assertRaises(ValueError, irq.Observer, observer.remote, observer.addresses, observer.rows[:-1])
        args = ['--firmware', '/unused', '--output', '/unused-output', '--exercise', 'irq']
        self.assertRaises(ValueError, policy.arguments, args)
        args += ['--patch-elf', '/input.elf', '--patch-manifest', '/input.json', '--causal-early-enable']
        self.assertTrue(policy.arguments(args).causal_early_enable)


class ProvenanceControls(unittest.TestCase):
    def inputs(self, root):
        data = bytes(runpy.run_path(str(Path(__file__).with_name('test-patch.py')))['elf_fixture']())
        sha = lambda value: hashlib.sha256(value).hexdigest()
        source = '1' * 64; sections = patching.parse_elf(data)
        identity = {sha(data): ('bip-v2', source, tuple(sha(sections[key]) for key in ('key', 'rx')))}
        value = dict(schema='aicwf-bip-patch-v1', state='assembled-not-executed', original_sha256=policy.ORIGINAL_SHA, original_size=policy.ORIGINAL_SIZE, firmware_base=policy.BASE,
            inputs={'aicwf-firmware-bip.S': source, 'aicwf-firmware-bip.ld': '000227df6210ffa6f25525c9bb04558b0d179cc238ebb2505b6a484f08186e46', 'aicwf-firmware-bip.py': '2' * 64},
            tools={name: dict(path='/other-host/' + name, sha256='3' * 64) for name in ('gcc', 'as', 'ld', 'collect2')}, sdk_api=1, sdk_inputs={name: '4' * 64 for name in ('__init__.py', 'process.py', 'paths.py', 'jsonio.py')}, python_version='3.15.0', compiler_version='16.2.0', compiler_target='arm-none-eabi', commands=[['cc'], ['ld']],
            sections=[dict(section=name, address=address, size=size, original_sha256=old, file=name[1:] + '.bin', replacement_sha256=sha(sections[key])) for key, name, address, size, old in patching.WINDOWS], device_access=False, execution_accepted=False, loader_accepted=False)
        elf = root / 'input.elf'; metadata = root / 'input.json'; elf.write_bytes(data)
        return data, identity, value, elf, metadata

    def test_portable_host_provenance_exact_schema_and_payload_gate(self):
        with tempfile.TemporaryDirectory(prefix='aic-admission-') as temporary:
            root = Path(temporary); data, identity, value, elf, metadata = self.inputs(root)
            with patch.dict(patching.PROFILES, identity, clear=True):
                metadata.write_text(json.dumps(value))
                self.assertEqual(patching.load(elf, metadata)['provenance']['tools'], value['tools'])
                value['schema'] = 'aicwf-bip-patch-v2'; value['modified_sha256'] = '5' * 64; value['kernel_header_sha256'] = '6' * 64
                metadata.write_text(json.dumps(value)); self.assertEqual(patching.load(elf, metadata)['modified_sha256'], '5' * 64)
                for field in ('kernel_header_sha256', 'modified_sha256'):
                    old = value.pop(field); metadata.write_text(json.dumps(value))
                    self.assertRaisesRegex(ValueError, 'schema or fields', patching.load, elf, metadata); value[field] = old
                value['schema'] = 'aicwf-bip-patch-v1'; metadata.write_text(json.dumps(value))
                self.assertRaisesRegex(ValueError, 'schema or fields', patching.load, elf, metadata)
                value['schema'] = 'aicwf-bip-patch-v2'; value['tools']['gcc']['sha256'] = 'bad'; metadata.write_text(json.dumps(value))
                self.assertRaisesRegex(ValueError, 'provenance', patching.load, elf, metadata)
                changed = bytearray(data); changed[128] ^= 1; elf.write_bytes(changed)
                self.assertRaisesRegex(ValueError, 'Unknown patch ELF', patching.load, elf, metadata)

    def test_modified_full_image_hash_before_output_creation(self):
        with tempfile.TemporaryDirectory(prefix='aic-image-') as temporary:
            root = Path(temporary); original = root / 'original'; original.write_bytes(bytes(policy.ORIGINAL_SIZE))
            replacements = {'key': bytes([7]) * 340, 'rx': bytes([9]) * 384}
            modified = bytearray(original.read_bytes())
            for name, data in replacements.items():
                start, end = policy.SPANS[name]; modified[start - policy.BASE:end - policy.BASE] = data
            p = dict(replacements=replacements, modified_sha256='0' * 64)
            with patch.object(policy, 'ORIGINAL_SHA', hashlib.sha256(original.read_bytes()).hexdigest()):
                self.assertRaisesRegex(ValueError, 'modified image', policy.prepare, original, root / 'bad', p, patched.inputs(irq=True))
                self.assertFalse((root / 'bad').exists())
                p['modified_sha256'] = hashlib.sha256(modified).hexdigest()
                policy.prepare(original, root / 'good', p, patched.inputs(irq=True))
                self.assertEqual((root / 'good/key.bin').read_bytes(), replacements['key'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
