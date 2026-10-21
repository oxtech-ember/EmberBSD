# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; finite model IRQ31 observation, not AIC interrupt timing.
import hashlib
import struct

import observations
import patched

# Reviewed v2 opcodes. The explicit early-enable mutant keeps their addresses.
LOCKED = {1: 0x142c10, 4: 0x142c10, 2: 0x165ef8}
ENTRY = {1: 0x142be0, 4: 0x142be0, 2: 0x165e48}
PROTECTED = ((0x142c10, 0x142d10), (0x165ef8, 0x165f60), (0x165f68, 0x165f6c))
SYMBOLS = {'harness_before_call', 'harness_call_returned', 'harness_before_release', 'harness_case_complete', 'harness_irq_entry', 'irq_count', 'irq_snapshot', 'active_case'}


class Observer:
    def __init__(self, remote, addresses, rows):
        if len(rows) != 36 or any(row['kind'] not in LOCKED or row.get('irq_phase') not in ('before', 'during', 'after') for row in rows):
            raise ValueError('Exactly the finite 36 IRQ scenarios are required')
        self.remote, self.addresses, self.rows = remote, addresses, rows
        self.breakpoints = {addresses['harness_done'] & ~1}
        self.last_pc = None
        self.paused = False
        self.events = []
        self.case = -1

    def event(self, name, **fields):
        self.events.append(dict(case=self.case, event=name, **fields))

    def add(self, address):
        address &= ~1
        if address not in self.breakpoints:
            self.remote.breakpoint(address)
            self.breakpoints.add(address)

    def remove(self, address):
        if address in self.breakpoints:
            self.remote.remove_breakpoint(address)
            self.breakpoints.remove(address)

    def word(self, address):
        return int.from_bytes(self.remote.memory(address, 4), 'little')

    def exception(self):
        # At our own vector entry, before its PUSH, hardware frame offset24 is PC.
        sp, lr = self.remote.register(13), self.remote.register(14)
        if lr != 0xfffffff9 or sp & 3:
            raise ValueError('Unsupported model exception frame')
        frame = struct.unpack('<8I', self.remote.memory(sp, 32))
        pc, xpsr = frame[6] & ~1, frame[7]
        if not xpsr & 0x01000000:
            raise ValueError('Model exception did not preserve Thumb state')
        protected = any(start <= pc < end for start, end in PROTECTED)
        self.event('irq-entry', stacked_pc=pc, exception_return=lr, protected=protected)
        if protected:
            raise ValueError('IRQ delivered inside protected BIP interval')
        return pc

    def stop(self, target):
        target &= ~1
        irq_entry = self.addresses['harness_irq_entry'] & ~1
        self.add(target)
        # Resume past the current hardware breakpoint with that breakpoint removed.
        if self.last_pc != irq_entry:
            self.add(irq_entry)
        self.paused = False
        reply = self.remote.command('c')
        if not reply.startswith((b'T05', b'S05')):
            raise ValueError('IRQ scenario did not stop at a breakpoint')
        self.paused = True
        pc = self.remote.register(15) & ~1
        self.last_pc = pc
        self.remove(pc)
        self.event('stop', pc=pc, expected=target)
        if pc != target:
            if pc == irq_entry:
                self.exception()
                raise ValueError('Unexpected IRQ before the admitted release')
            raise ValueError('IRQ scenario reached an unexpected PC')
        if pc == irq_entry:
            self.exception()

    def count_pending(self, count, pending):
        actual_count = self.word(self.addresses['irq_count'])
        actual_pending = bool(self.word(0xe000e200) & 0x80000000)
        self.event('count-pending', count=actual_count, pending=actual_pending)
        if actual_count != count or actual_pending != pending:
            raise ValueError('IRQ count/pending checkpoint differs')

    def snapshot(self, row, state):
        c = patched.context(row); vif = row['vif'] * observations.VIF_SIZE
        wanted = state[c:c + 120] + state[vif + 0x4b0:vif + 0x4b8]
        actual = self.remote.memory(self.addresses['irq_snapshot'], 128)
        self.event('snapshot', sha256=hashlib.sha256(actual).hexdigest(), expected_sha256=hashlib.sha256(wanted).hexdigest())
        if actual != wanted:
            raise ValueError('IRQ observed an incomplete transaction snapshot')

    def returned_mask(self, index, mask):
        actual = self.word(self.addresses['results'] + index * observations.RECORD_BYTES + 12)
        self.event('returned-mask', primask=actual, expected=mask)
        if actual != mask:
            raise ValueError('IRQ scenario changed the returned PRIMASK')

    def run(self):
        a = self.addresses
        for index, row in enumerate(self.rows):
            self.case = index
            phase, mask = row['irq_phase'], row['mask']
            before = patched.before_state(row)
            after = patched.expected(row, before)[0]
            self.stop(a['harness_before_call'])
            if self.word(a['active_case']) != index:
                raise ValueError('IRQ active case identity differs')
            self.count_pending(0, False)
            if phase == 'before':
                self.remote.pend_irq31(); self.event('pend', phase=phase)
                if mask == 0:
                    self.stop(a['harness_irq_entry'])
                    self.stop(ENTRY[row['kind']])
                    self.count_pending(1, False)
                    self.snapshot(row, before)
                else:
                    self.stop(LOCKED[row['kind']])
                    self.count_pending(0, True)
            elif phase == 'during':
                self.stop(LOCKED[row['kind']])
                self.count_pending(0, False)
                self.remote.pend_irq31(); self.event('pend', phase=phase)
            if phase == 'during' and mask == 0:
                self.stop(a['harness_irq_entry'])
            self.stop(a['harness_call_returned'])
            self.returned_mask(index, mask)
            if phase == 'after':
                self.count_pending(0, False)
                self.remote.pend_irq31(); self.event('pend', phase=phase)
                if mask == 0:
                    self.stop(a['harness_irq_entry'])
            self.stop(a['harness_before_release'])
            self.returned_mask(index, mask)
            self.count_pending(1 if mask == 0 else 0, bool(mask))
            if mask == 0:
                self.snapshot(row, before if phase == 'before' else after)
            if mask == 1:
                self.stop(a['harness_irq_entry'])
            self.stop(a['harness_case_complete'])
            self.count_pending(1, False)
            self.snapshot(row, before if phase == 'before' and mask == 0 else after)
            self.event('case-complete', name=row['name'])
        self.stop(a['harness_done'])
        return dict(accepted_bounded_irq_fixture=True, selected_scenarios=len(self.rows), full_instruction_coverage=False, irq_interleavings_qualified=False, silicon_timing_qualified=False)
