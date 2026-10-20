/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; actual native engine ordering/fault controls. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static unsigned checks;
#define CHECK(c) do { checks++; if (!(c)) { \
 fprintf(stderr, "check %u failed at line %d: %s\n", checks, __LINE__, #c); exit(1); \
} } while (0)
#define KASSERT(c) CHECK(c)
#define MUTEX_DEFAULT 0
#define IPL_VM 5
#define FDT_INTR_MPSAFE 1
#define BUS_SPACE_BARRIER_WRITE 2
#define BUS_SPACE_BARRIER_READ 1
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef size_t bus_size_t;
typedef uint64_t bus_addr_t;
typedef struct { bool held; int ipl; } kmutex_t;
struct clk { unsigned id; };
struct sun60i_a733_npu_dma { bool retained; size_t bytes; };
struct sun60i_a733_npu_dma_info {
 uint64_t mtlb_address, stlb_address, command_address;
 uint32_t pd_entry, command_virtual;
 size_t command_bytes; bool retained; uint32_t profile;
};
#define A733_NPU_NN_COMMAND_BYTES 144
#define A733_NPU_DMA_PROFILE_FE 1
#define A733_NPU_DMA_PROFILE_TASK 2
#include "sun60i_a733_npu_run.h"
#include "sun60i_a733_npu_fe.h"
static struct sun60i_a733_npu_run state;
static struct sun60i_a733_npu_dma context;
static struct clk clock1;
static const int cookie;
static int bus_token, irq_token;
static uint32_t regs[1024], write_offsets[64], values[64];
static unsigned maps, unmaps, established, removed, proofs, peeks, pokes;
static unsigned writes, ordinary_reads, barriers, delays, setters, retains;
static unsigned fail_proof, fail_peek, fail_poke;
static bool fail_map, fail_irq, fail_set, fail_retain, bad_mask, no_irq;
static bool in_irq, late_error, pending_irq, start_drained;
static int property_bytes;
static uint32_t irq_value;
static unsigned bad_route, unstable_peek, mismatch_step, bad_info, infos, fail_info;
static bool prestart_irq, clean_retain;
static uint32_t terminal_idle, terminal_address;
static bool mask_drained, address_drained, terminal_unstable;
static unsigned dma_reads;
static uint32_t job_status_xor, fresh_reset_unknown;
static uint32_t command_readback, command_requested;
static unsigned command_writes;
static bool lose_command_address;
static int (*callback)(void *);
static void *callback_arg;
static void mutex_init(kmutex_t *m, int type, int ipl)
{ CHECK(type == MUTEX_DEFAULT && ipl == IPL_VM); m->held = false; m->ipl = ipl; }
static void mutex_enter(kmutex_t *m)
{ CHECK(m->ipl == IPL_VM && !m->held); m->held = true; }
static void mutex_exit(kmutex_t *m)
{
 CHECK(m->held); m->held = false;
 if (pending_irq && m == &state.irq_lock) {
  CHECK(start_drained); pending_irq = false; in_irq = true;
  CHECK(callback(callback_arg) == (irq_value == 0 ? 0 : 1)); in_irq = false;
 }
}
static int fdtbus_intr_parent(int phandle)
{ CHECK(phandle == 7); return bad_route == 1 ? -1 : 8; }
static bool fdtbus_status_okay(int phandle)
{ CHECK(phandle == 7 || phandle == 8); return bad_route != (phandle == 7 ? 2U : 3U); }
static int of_compatible(int phandle, const char * const *names)
{ CHECK(phandle == 8 && !strcmp(names[0], "arm,gic-v3") && names[1] == NULL); return bad_route != 4; }
static int OF_getproplen(int phandle, const char *name)
{
 CHECK(phandle == 7 || phandle == 8);
 if (phandle == 7) {
  if (!strcmp(name, "interrupts")) return property_bytes;
  CHECK(!strcmp(name, "interrupts-extended")); return bad_route == 5 ? 12 : -1;
 }
 if (!strcmp(name, "interrupt-controller")) return bad_route == 6 ? -1 : 0;
 if (!strcmp(name, "#interrupt-cells")) return bad_route == 7 ? 8 : 4;
 CHECK(!strcmp(name, "reg")); return bad_route == 8 ? 24 : 16;
}
static int of_getprop_uint32_array(int phandle, const char *name,
    uint32_t *out, int n)
{
 if (phandle == 8) {
  CHECK(!strcmp(name, "#interrupt-cells") && n == 1);
  *out = bad_route == 9 ? 2 : 3; return bad_route == 10 ? EIO : 0;
 }
 CHECK(phandle == 7 && !strcmp(name, "interrupts") && n == 3);
 out[0] = bad_route == 11 ? 1 : 0;
 out[1] = bad_route == 12 ? 64 : 65;
 out[2] = bad_route == 13 ? 1 : 4; return bad_route == 14 ? EIO : 0;
}
static int fdtbus_get_reg(int phandle, unsigned slot, bus_addr_t *addr, bus_size_t *size)
{
 CHECK(phandle == 8 && slot < 2);
 *addr = slot == 0 ? 0x03400000 : 0x03460000;
 *size = slot == 0 ? 0x10000 : 0x100000;
 if (bad_route == 15 + slot * 3) return EIO;
 if (bad_route == 16 + slot * 3) (*addr)++;
 if (bad_route == 17 + slot * 3) (*size)++;
 return 0;
}
static int sun60i_a733_ccu_npu_ready(struct clk *clock, const void *owner)
{
 CHECK(!in_irq && clock == &clock1 && owner == &cookie);
 proofs++;
 if (prestart_irq && proofs == 9) {
  regs[4] = irq_value; in_irq = true;
  CHECK(callback(callback_arg) == 1); in_irq = false;
 }
 return proofs == fail_proof ? EIO : 0;
}
static int sun60i_a733_pck_npu_ready(int power, const void *owner)
{
 CHECK(!in_irq && power == 9 && owner == &cookie);
 return ++proofs == fail_proof ? EIO : 0;
}
static int bus_space_map(bus_space_tag_t tag, uint64_t addr, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
 CHECK(!in_irq && tag == &bus_token && addr == 0x03600000 && size == 4096 && !flags);
 maps++; if (fail_map) return ENOMEM; *handle = 0x55; return 0;
}
static void bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t bytes)
{ CHECK(!in_irq && !context.retained && tag == &bus_token && handle == 0x55 && bytes == 4096); unmaps++; }
static bool valid_offset(bus_size_t offset)
{
 static const unsigned allowed[] = { 0,4,0x10,0x14,0x24,0x30,0x388,0x3a4,
  0x3b4,0x654,0x664,0x668,0x66c,0x90,0x100,0x104,0x10c,0x3a8,0x414 };
 for (size_t i = 0; i < sizeof(allowed)/sizeof(allowed[0]); i++)
  if (offset == allowed[i]) return true;
 return false;
}
static int bus_space_peek_4(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset, uint32_t *out)
{
 CHECK(!in_irq && tag == &bus_token && h == 0x55 && valid_offset(offset));
 if (++peeks == fail_peek) return EFAULT;
 *out = regs[offset / 4];
 if (peeks == unstable_peek) *out ^= 2;
 if (mismatch_step && state.status.step == mismatch_step && pokes != 0 &&
     offset == write_offsets[writes - 1]) {
  *out ^= offset == 0x10c ? 0x20000 : offset == 0x90 ? 0x40 : 1;
  mismatch_step = 0;
 }
 if (offset == 0x14 && pokes && values[writes - 1] == 0) CHECK(mask_drained);
 if (offset == 0x664 && state.status.started && terminal_unstable && ++dma_reads % 2 == 0) *out ^= 8;
 if (offset == 0x10) { CHECK(state.status.publication_attempted && state.irq_lock.held); regs[4] = 0; }
 return 0;
}
static void record_write(bus_size_t offset, uint32_t value)
{
 CHECK(context.retained && state.status.publication_attempted && writes < 64);
 write_offsets[writes] = (uint32_t)offset; values[writes++] = value;
}
static void bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset, uint32_t value)
{
 CHECK(in_irq && state.irq_lock.held && tag == &bus_token && h == 0x55);
 CHECK(offset == 0x14 && value == 0); record_write(offset, value); regs[5] = value; mask_drained = false;
 if (late_error) regs[4] = 0x40000000;
}
static uint32_t bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset)
{
 CHECK(in_irq && state.irq_lock.held && tag == &bus_token && h == 0x55);
 CHECK(offset == 0x10 || offset == 0x14); ordinary_reads++;
 uint32_t value = regs[offset / 4];
 if (offset == 0x10) regs[4] = 0;
 if (offset == 0x14) CHECK(mask_drained);
 if (offset == 0x14 && bad_mask) value = 1;
 return value;
}
static void bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset, bus_size_t bytes, int flags)
{
 CHECK(tag == &bus_token && h == 0x55 && bytes == 4);
 CHECK((offset == 0x14 || offset == 0x654 || offset == 0x3a4) && flags == BUS_SPACE_BARRIER_WRITE);
 if (in_irq) CHECK(offset == 0x14 && state.irq_lock.held);
 if (offset == 0x14) mask_drained = true;
 if (offset == 0x654) address_drained = true;
 if (offset == 0x3a4) { CHECK(state.irq_lock.held); start_drained = true; }
 barriers++;
}
static int bus_space_poke_4(bus_space_tag_t tag, bus_space_handle_t h,
    bus_size_t offset, uint32_t value)
{
 CHECK(!in_irq && tag == &bus_token && h == 0x55 && valid_offset(offset));
 CHECK(context.retained && state.status.publication_attempted);
 if (++pokes == fail_poke) return EFAULT;
 uint32_t before = regs[offset / 4];
 record_write(offset, value); regs[offset / 4] = value;
 if (offset == 0x3a8)
  regs[offset / 4] = ((value & ~0x10U) | (before & 0x10U)) ^ job_status_xor;
 if (pokes == 2) regs[0x3a8 / 4] |= fresh_reset_unknown;
 if (offset == 0x14 && value == 0) mask_drained = false;
 if (offset == 0x654) {
  command_writes++; address_drained = false;
  if (!lose_command_address) command_requested = value;
  regs[offset / 4] = command_readback;
 }
 if (offset == 0x3a4) {
  CHECK(value == 0x00010200 && command_writes == 1);
  CHECK(command_requested == (lose_command_address ? 0 : 0x01001000));
  CHECK(state.status.started && state.irq_lock.held && address_drained);
  /* A zero echo and a synthetic event do not prove that FE fetched a command. */
  regs[0x664 / 4] = lose_command_address ? 0 : terminal_address;
  regs[1] = terminal_idle; regs[4] = irq_value;
  if (!no_irq) pending_irq = true;
 }
 return 0;
}
static void *fdtbus_intr_establish_xname(int phandle, unsigned slot, int ipl,
    int flags, int (*fn)(void *), void *arg, const char *name)
{
 CHECK(!in_irq && phandle == 7 && slot == 0 && ipl == IPL_VM);
 CHECK(flags == FDT_INTR_MPSAFE && name != NULL && context.retained);
 established++; if (fail_irq) return NULL;
 callback = fn; callback_arg = arg;
 unsigned reads = peeks + ordinary_reads;
 in_irq = true; CHECK(fn(arg) == 0); in_irq = false;
 CHECK(peeks + ordinary_reads == reads); return &irq_token;
}
static void fdtbus_intr_disestablish(int phandle, void *ih)
{ CHECK(!in_irq && !context.retained && phandle == 7 && ih == &irq_token); removed++; }
static void delay(unsigned us)
{ CHECK(!in_irq && us == 1000); delays++; CHECK(delays <= 100); }
static int sun60i_a733_npu_dma_info(const struct sun60i_a733_npu_dma *m,
    struct sun60i_a733_npu_dma_info *out)
{
 CHECK(!in_irq && m == &context);
 if (++infos == fail_info) return EIO;
 *out = (struct sun60i_a733_npu_dma_info){ 0x100000,0x110000,0x120000,
  0x1001,0x01001000,m->bytes,m->retained,A733_NPU_DMA_PROFILE_FE };
 if (infos == 1) {
  switch (bad_info) {
  case 1: out->retained = true; break;
  case 2: out->command_virtual = 0x02001000; break;
  case 3: out->command_address = out->command_virtual; break;
  case 4: out->command_bytes = 40; break;
  case 5: out->pd_entry ^= 1; break;
  case 6: out->profile = A733_NPU_DMA_PROFILE_TASK; break;
  case 7: out->profile = 0; break;
  }
 }
 return 0;
}

static int sun60i_a733_npu_dma_set_commands(struct sun60i_a733_npu_dma *m,
    const void *bytes, size_t count)
{
 static const uint8_t expected[40] = {
  0x61,0,1,8,0x7f,0xff,0xff,0xff,2,0x0e,1,8,1,7,0,0x30,
  0,0,0,0x48,1,7,0,0x30,1,0x0e,1,8,0x44,0,0,0,0,0,0,0x10,0,0,0,0 };
 CHECK(!in_irq && m == &context && !m->retained && count == 40);
 CHECK(memcmp(bytes, expected, 40) == 0); setters++;
 if (fail_set)
  return EIO;
 m->bytes = count;
 return 0;
}
static int sun60i_a733_npu_dma_retain(struct sun60i_a733_npu_dma *m)
{
 CHECK(!in_irq && m == &context && !m->retained && state.status.publication_attempted); retains++;
 m->retained = !clean_retain; return fail_retain ? EIO : 0;
}
#include "sun60i_a733_npu_fe.c"
#include "sun60i_a733_npu_run.c"
static void reset(void)
{
 memset(&state, 0, sizeof(state)); memset(regs, 0, sizeof(regs));
 maps=unmaps=established=removed=proofs=peeks=pokes=writes=ordinary_reads=barriers=delays=setters=retains=0;
 fail_proof=fail_peek=fail_poke=0;
 fail_map=fail_irq=fail_set=fail_retain=bad_mask=no_irq=in_irq=late_error=false;
 pending_irq=start_drained=prestart_irq=clean_retain=false;
 unstable_peek=mismatch_step=bad_info=infos=fail_info=0;
 mask_drained=address_drained=terminal_unstable=false; dma_reads=0;
 job_status_xor=fresh_reset_unknown=0; regs[0x3a8/4]=0x10;
 command_readback=command_requested=command_writes=0; lose_command_address=false;
 terminal_idle=0x7fffffff; terminal_address=0x01001028;
 callback=NULL; callback_arg=NULL; property_bytes=12; irq_value=0x10; bad_route=0;
 context=(struct sun60i_a733_npu_dma){false,16};
 regs[1]=0x7fffffff; regs[0x24/4]=0x9202; regs[0x30/4]=0x1000003b;
 regs[0x10c/4]=0x1000000; regs[0x90/4]=0x80000000; regs[0x414/4]=3;
}
static int run(void)
{ return sun60i_a733_npu_run_once(&state,&bus_token,&clock1,9,&cookie,7,"npu",&context); }

static void
completion_contract(void)
{
	struct sun60i_a733_npu_dma other = { 0 };
	struct sun60i_a733_npu_run_status saved;
	unsigned io;

	reset();
	CHECK(!sun60i_a733_npu_run_task_completed(NULL, &context));
	CHECK(!sun60i_a733_npu_run_task_completed(&state, NULL));
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	CHECK(run() == 0);
	io = peeks + ordinary_reads + writes + maps + removed;
	/* Real FE completion cannot grant a TASK output read. */
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	state.profile = A733_NPU_DMA_PROFILE_TASK;
	state.bound_dma = &context;
	state.status.command_bytes = 144;
	/* This cache-only fixture state is not a hardware TASK result. */
	CHECK(sun60i_a733_npu_run_task_completed(&state, &context));
	saved = state.status;
	uint32_t *required_one[] = {
		&state.status.attempted, &state.status.publication_attempted,
		&state.status.retained, &state.status.started,
		&state.status.terminal, &state.status.snapshot_valid
	};
	for (unsigned i = 0;
	    i < sizeof(required_one) / sizeof(required_one[0]); i++) {
		for (unsigned value = 0; value <= 2; value += 2) {
			*required_one[i] = value;
			CHECK(!sun60i_a733_npu_run_task_completed(&state,
			    &context));
			state.status = saved;
		}
	}
	uint32_t *required_zero[] = {
		&state.status.error, &state.status.irq_fault,
		&state.status.irq_mask_readback, &state.status.terminal_status
	};
	for (unsigned i = 0;
	    i < sizeof(required_zero) / sizeof(required_zero[0]); i++) {
		*required_zero[i] = 1;
		CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
		state.status = saved;
	}
	uint32_t *literals[] = {
		&state.status.step, &state.status.command_bytes,
		&state.status.irq_raw, &state.status.irq_bits,
		&state.status.terminal_idle[0], &state.status.terminal_idle[1],
		&state.status.terminal_dma[0], &state.status.terminal_dma[1]
	};
	for (unsigned i = 0; i < sizeof(literals) / sizeof(literals[0]); i++) {
		*literals[i] ^= 1;
		CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
		state.status = saved;
	}
	state.status.irq_baseline = state.status.irq_count;
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	state.status = saved;
	const uint32_t badva[] = { 0, 0x01000fff, 0x01002000, UINT32_MAX };
	for (unsigned i = 0; i < sizeof(badva) / sizeof(badva[0]); i++) {
		state.status.terminal_dma[0] = state.status.terminal_dma[1] =
		    badva[i];
		CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
		state.status = saved;
	}
	state.armed = true;
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	state.armed = false;
	state.mapped = false;
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	state.mapped = true;
	void *ih = state.ih;
	state.ih = NULL;
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	state.ih = ih;
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &other));
	state.bound_dma = &other;
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	state.bound_dma = &context;
	state.profile = A733_NPU_DMA_PROFILE_FE;
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	state.profile = 0;
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	state.profile = A733_NPU_DMA_PROFILE_TASK;
	state.lock_initialized = false;
	CHECK(!sun60i_a733_npu_run_task_completed(&state, &context));
	state.lock_initialized = true;
	CHECK(sun60i_a733_npu_run_task_completed(&state, &context));
	CHECK(io == peeks + ordinary_reads + writes + maps + removed);
}

int main(void)
{
 completion_contract();
 reset(); int result=run();
 if (result != 0)
  fprintf(stderr,"command-address baseline: error=%d step=%u readback=%#x started=%u retained=%u irq=%llu\n",
      result,state.status.step,state.status.last_readback,state.status.started,
      state.status.retained,(unsigned long long)state.status.irq_count);
 CHECK(result==0);
 CHECK(state.status.retained && state.status.terminal && state.status.started);
 CHECK(state.status.irq_count > state.status.irq_baseline && state.status.irq_bits==0x10);
 CHECK(state.status.irq_mask_readback==0 && state.status.terminal_status==0);
 CHECK(maps==1 && unmaps==0 && established==1 && removed==0 && context.bytes==40);
 unsigned complete_peeks = peeks;
 CHECK(writes>=14 && write_offsets[0]==0 && values[0]==0x70900);
 CHECK(write_offsets[1]==0 && values[1]==0x70100 && write_offsets[2]==0x3a8 && values[2]==2);
 CHECK(write_offsets[3]==0x100 && values[3]==0x140021);
 CHECK(write_offsets[4]==0x3b4 && values[4]==0x1001 && write_offsets[5]==0x388 && values[5]==0x21);
 CHECK(values[7]==0x1020000 && values[8]==0x80000040 && values[9]==2);
 CHECK(regs[0x3a8/4]==0x12);
 CHECK(state.status.last_readback==0 && command_writes==1 && address_drained);
 CHECK(write_offsets[11]==0x654 && values[11]==0x01001000);
 reset(); command_readback=0x01001000;
 CHECK(run()==0 && state.status.terminal && command_writes==1);
 CHECK(state.status.last_readback==0x01001000 && address_drained);
 for (unsigned bit=0; bit<32; bit++) {
  uint32_t candidates[2] = { UINT32_C(1)<<bit, UINT32_C(0x01001000)^(UINT32_C(1)<<bit) };
  for (unsigned i=0; i<2; i++) {
   reset(); command_readback=candidates[i];
   CHECK(command_readback!=0 && command_readback!=0x01001000);
   CHECK(run()==EIO && state.status.step==12 && state.status.last_readback==command_readback);
   CHECK(command_writes==1 && address_drained && context.retained);
   CHECK(!state.status.started && !state.status.terminal && state.status.irq_count==0);
   CHECK(!unmaps && !removed && regs[0x3a4/4]==0);
  }
 }
 const uint32_t alternate[] = {0x01001004,0x01000000,0x120000,0xffffffff};
 for (unsigned i=0; i<sizeof(alternate)/sizeof(alternate[0]); i++) {
  reset(); command_readback=alternate[i];
  CHECK(run()==EIO && state.status.step==12 && state.status.last_readback==alternate[i]);
  CHECK(command_writes==1 && !state.status.started && !state.status.terminal && context.retained);
 }
 reset(); lose_command_address=true;
 CHECK(run()==ETIMEDOUT && context.retained && !state.status.terminal);
 CHECK(state.status.started && state.status.irq_bits==0x10 && state.status.irq_count==1);
 CHECK(regs[1]==0x7fffffff && state.status.terminal_dma[0]==0 && state.status.terminal_dma[1]==0);
 CHECK(command_writes==1 && address_drained && !unmaps && !removed);
 for (unsigned status=0; status<=1; status++) {
  for (unsigned flip=0; flip<=1; flip++) {
   reset(); regs[0x3a8/4]=status*0x10; job_status_xor=flip*0x10;
   CHECK(run()==0 && state.status.terminal);
   CHECK(write_offsets[2]==0x3a8 && values[2]==2);
   CHECK(regs[0x3a8/4]==(2|((status^flip)*0x10)));
  }
 }
 for (unsigned bit=0; bit<32; bit++) {
  if (bit==4) continue;
  reset(); regs[0x3a8/4]|=UINT32_C(1)<<bit;
  CHECK(run()==EBUSY && state.status.snapshot_valid);
  CHECK(!writes && !retains && !established && !state.status.publication_attempted && unmaps==1);
  reset(); fresh_reset_unknown=UINT32_C(1)<<bit;
  CHECK(run()==EBUSY && context.retained && !state.status.started && !state.status.terminal);
  CHECK(writes==3 && write_offsets[0]==0 && write_offsets[1]==0 && write_offsets[2]==0x14);
  CHECK(values[2]==0 && !unmaps && !removed && regs[0x3a4/4]==0);
  reset(); job_status_xor=UINT32_C(1)<<bit;
  CHECK(run()==EIO && state.status.step==3 && context.retained);
  CHECK(values[2]==2 && write_offsets[2]==0x3a8 && writes==4 && write_offsets[3]==0x14);
  CHECK(!state.status.started && !state.status.terminal && !unmaps && !removed && regs[0x3a4/4]==0);
 }
 reset(); CHECK(run()==0);
 unsigned oldio=peeks+pokes+ordinary_reads+maps+proofs;
 CHECK(run()==EALREADY && oldio==peeks+pokes+ordinary_reads+maps+proofs);
 struct sun60i_a733_npu_run_status copy;
 sun60i_a733_npu_run_status(&state,&copy); CHECK(copy.terminal && oldio==peeks+pokes+ordinary_reads+maps+proofs);
 for(unsigned i=1;i<=10;i++) { reset(); fail_proof=i; CHECK(run()!=0); CHECK(!state.status.terminal); if(context.retained) CHECK(regs[5]==0); }
 for(unsigned i=1;i<=36;i++) { reset(); fail_peek=i; CHECK(run()!=0); CHECK(!writes && !context.retained && unmaps==1); }
 for(unsigned i=1;i<=14;i++) { reset(); fail_poke=i; CHECK(run()!=0); CHECK(context.retained && !unmaps && !removed && !state.status.terminal); CHECK(writes && write_offsets[writes-1]==0x14 && values[writes-1]==0); }
 for (unsigned i=37; i<=complete_peeks; i++) {
  reset(); fail_peek=i; CHECK(run()!=0);
  CHECK(context.retained && !unmaps && !removed && !state.status.terminal);
 }
 for (unsigned i=19; i<=36; i++) {
  reset(); unstable_peek=i; CHECK(run()==EBUSY);
  CHECK(state.status.snapshot_valid && !writes && !retains && unmaps==1);
 }
 for (unsigned i=1; i<=12; i++) {
  reset(); mismatch_step=i; CHECK(run()==EIO);
  CHECK(context.retained && !state.status.terminal && !unmaps && !removed);
 }
 for (unsigned i=1; i<=7; i++) {
  reset(); bad_info=i; CHECK(run()==EINVAL && !maps && !writes && !retains && !established);
 }
 for (unsigned i=1; i<=2; i++) {
  reset(); fail_info=i; CHECK(run()!=0 && !writes && !established);
  CHECK(i==1 ? (!maps && !retains) : (context.retained && !unmaps));
 }
 reset(); clean_retain=true; CHECK(run()==EIO && !writes && !unmaps && !established);
 CHECK(state.status.publication_attempted && !state.status.retained);
 for (unsigned i=0; i<3; i++) {
  reset();
  regs[(i==0 ? 0 : i==1 ? 0x10c : 0x3a8)/4] |= i==0 ? 0x80000 : 1;
  CHECK(run()==EBUSY && !writes && !retains && unmaps==1);
 }
 reset(); prestart_irq=true; CHECK(run()==EIO && state.status.irq_fault && !state.status.started);
 CHECK(context.retained && !unmaps && !removed && regs[0x3a4/4]==0);
 reset(); irq_value=0; CHECK(run()==ETIMEDOUT && !state.status.terminal && state.status.irq_count==0);
 reset(); state.status.irq_count=17; no_irq=true;
 CHECK(run()==ETIMEDOUT && state.status.irq_baseline==17 && !state.status.terminal);
 reset(); state.status.irq_count=UINT64_MAX;
 CHECK(run()==EIO && state.status.irq_count==UINT64_MAX && state.status.irq_fault);
 for (unsigned i=0; i<4; i++) {
  reset();
  if (i==0) terminal_idle=0;
  if (i==1) terminal_address=0x01000fff;
  if (i==2) terminal_address=0x01002000;
  if (i==3) terminal_address=0xffffffff;
  CHECK(run()==ETIMEDOUT && !state.status.terminal && delays==100 && context.retained);
 }
 reset(); terminal_unstable=true; CHECK(run()==ETIMEDOUT && !state.status.terminal && delays==100);
 reset(); fail_map=true; CHECK(run()==ENOMEM && !peeks && !writes);
 reset(); fail_irq=true; CHECK(run()!=0 && !writes && !unmaps && context.retained);
 reset(); fail_set=true; CHECK(run()!=0 && !writes && !context.retained);
 reset(); fail_retain=true; CHECK(run()!=0 && context.retained && !unmaps && !removed);
 reset(); regs[1]=1; CHECK(run()==EBUSY && !writes && !retains);
 reset(); regs[5]=1; CHECK(run()==EBUSY && !writes && !retains);
 reset(); bad_mask=true; CHECK(run()!=0 && state.status.irq_fault && !state.status.terminal);
 reset(); late_error=true; CHECK(run()!=0 && state.status.terminal_status==0x40000000);
 reset(); no_irq=true; CHECK(run()==ETIMEDOUT && !state.status.terminal && delays<=100);
 reset(); irq_value=0x40000010; CHECK(run()!=0 && !state.status.terminal);
 for (unsigned i = 1; i <= 20; i++) { reset(); bad_route=i; CHECK(run()==EINVAL && !maps && !proofs && !writes && !retains && !established); }
 reset(); property_bytes=16; CHECK(run()!=0 && !maps && !proofs && !writes);
 printf("A733 NPU Engine2 run: %u checks passed\n",checks); return 0;
}
