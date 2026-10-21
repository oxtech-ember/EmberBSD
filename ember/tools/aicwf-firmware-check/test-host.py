#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; host-only U02 instruction harness policy checks.
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import tempfile
import time
from types import SimpleNamespace
from unittest.mock import patch
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
import oracle
import policy
import cases
from remote import Remote

class HostTests(unittest.TestCase):
    def test_rfc4493_independent_oracle(self):
        key=bytes.fromhex('2b7e151628aed2a6abf7158809cf4f3c')
        message=bytes.fromhex('6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710')
        for length,expected in ((0,'bb1d6929e95937287fa37d129b756746'),(16,'070a16b46b4d4144f79bdd9dd04a287c'),(40,'dfa66747de9ae63030ca32611497c827'),(64,'51f0bebf7e3b9d92fc49741779363cfe')):
            self.assertEqual(oracle.cmac(key,message[:length]).hex(),expected)
    def test_aes_known_answer(self):
        self.assertEqual(oracle.aes(bytes.fromhex('000102030405060708090a0b0c0d0e0f'),bytes.fromhex('00112233445566778899aabbccddeeff')).hex(),'69c4e0d86a7b0430d8cdb78070b4c55a')
    def test_wrong_original_before_output(self):
        with tempfile.TemporaryDirectory(prefix='aic-u02-host-') as temporary:
            root=Path(temporary); original=root/'unknown.bin'; original.write_bytes(b'unknown firmware')
            self.assertRaisesRegex(ValueError,'identity',policy.prepare,original,root/'output')
            self.assertFalse((root/'output').exists())
    def test_selected_spans_exclude_boot_and_are_disjoint(self):
        spans=sorted(policy.SPANS.values())
        self.assertEqual(len(spans),9)
        self.assertTrue(all(0x120000<start<end<=0x120000+341200 for start,end in spans))
        self.assertTrue(all(left[1]<=right[0] for left,right in zip(spans,spans[1:])))
        self.assertEqual(sum(end-start for start,end in spans),3928)
    def test_bip_oracle_masks_fc_and_keeps_all_six_pn_bytes(self):
        frame=oracle.frame(4,0x100000001,b'\x06\x00')
        self.assertEqual(frame[-14:-8],b'\x01\x00\x00\x00\x01\x00')
        key=bytes(range(16));mic=oracle.bip(key,frame)
        self.assertNotEqual(mic,oracle.bip(key,oracle.frame(4,1,b'\x06\x00')))
        changed=bytearray(frame);changed[1]^=0x38
        self.assertEqual(mic,oracle.bip(key,bytes(changed)))
    def test_refuse_unknown_or_repeated_arguments(self):
        for args in (['--unknown'],['--firmware','a','--firmware','b'],['--mode','run','--mode','prepare']):
            self.assertRaises((ValueError,SystemExit),policy.arguments,args)
    def test_direct_cmac_inputs_do_not_include_their_expected_mic(self):
        rows=cases.inputs()
        cmacs=[row for row in rows if row['kind']==3]
        self.assertEqual(len(cmacs),7)
        self.assertTrue(all(row['frame'][-8:]==bytes(8) for row in cmacs))
        self.assertTrue(all(row['frame'][-8:]!=bytes(8) for row in rows if row['kind']==2))
    def test_trap_layout_covers_holes_without_overwriting_original_spans(self):
        layout=policy.layout()
        self.assertEqual(layout[0][0],0x10000)
        self.assertEqual(layout[-1][1],0x180000)
        self.assertTrue(all(left[1]==right[0] for left,right in zip(layout,layout[1:])))
        originals=[(start,end) for start,end,name in layout if name.startswith('.original.')]
        self.assertEqual(sorted(originals),sorted(policy.SPANS.values()))
    def test_rsp_ok_is_not_discarded_as_console_output(self):
        class Socket:
            data=bytearray(b'+$OK#9a')
            def sendall(self,data):pass
            def recv(self,count):
                value=bytes(self.data[:count]);del self.data[:count];return value
        remote=Remote.__new__(Remote)
        remote.socket=Socket();remote.deadline=time.monotonic()+1
        remote.owner=SimpleNamespace(poll=lambda:None)
        self.assertEqual(remote.command('Z1,400,2'),b'OK')
        remote.deadline=time.monotonic()-1
        self.assertRaises(TimeoutError,remote.remaining)
    def test_only_frontend_freestanding_build_commands(self):
        commands=policy.build_commands(Path('/tools'),Path('/work'),Path('/source'))
        for argv in commands:
            self.assertEqual(argv[0],'/tools/bin/arm-none-eabi-gcc')
            self.assertIn('-mthumb',argv);self.assertIn('-mcpu=cortex-m4',argv);self.assertIn('-mfloat-abi=soft',argv)
            self.assertFalse(any(arg.startswith('--sysroot') for arg in argv))
        self.assertIn('-nostdlib',commands[-1])
        self.assertFalse(any(arg in ('-lc','-lgcc') for argv in commands for arg in argv))

if __name__=='__main__':unittest.main(verbosity=2)
