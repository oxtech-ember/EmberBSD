/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; actual native coordinator fault controls. */
#define main dma_fixture_unused_main
#include "a733-de352-dma-contract.c"
#undef main
#include <stdarg.h>
#include <stddef.h>
#include <time.h>
#include "display-memory-regs.h"
#define CTASSERT(x) _Static_assert(x, #x)

typedef size_t bus_size_t;
typedef unsigned bus_space_handle_t;
typedef void *bus_space_tag_t;
typedef void *device_t;
typedef void *cfdata_t;
typedef int kmutex_t;
struct clk { int value; };
struct sysctllog { int unused; };
struct sysctlnode { void *sysctl_data; size_t sysctl_size; };
struct device_compatible_entry { const char *compat; };
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; bus_dma_tag_t faa_dmat; };
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define ONCE_DECL(n) int n
#define RUN_ONCE(p, fn) do { if (!*(p)) { CHECK((fn)() == 0); *(p) = 1; } } while (0)
#define KASSERT(x) CHECK(x)
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define CTLTYPE_NODE 1
#define CTLTYPE_INT 2
#define CTLTYPE_STRUCT 6
#define CTL_HW 3
#define CTL_CREATE 4
#define CTL_EOL 5
#define CTLFLAG_READWRITE 1
#define CTLFLAG_READONLY 0
#define SYSCTL_DESCR(x) x
#define SYSCTLFN_ARGS const struct sysctlnode *rnode, const void *newp
#define SYSCTLFN_CALL(x) x, newp
#define CFATTACH_DECL_NEW(n,s,m,a,d,r)
#define be32toh(x) __builtin_bswap32(x)

static uint64_t tick;
static int reads, writes, space_maps, space_unmaps, releases, faults, begins;
static int power_error, power_releases, ccu_release_error, pck_release_error, ccu_faults;
static int reserve_error, memory_error, begin_error, mode_error, prepare_error, start_error;
static bool attempt_begin, claimed, memory_claimed, retained, started, irq_masked, power_claimed;
static bool no_busy, no_finish, no_copy, no_vblank, same_sequence, bad_w1c;
static int read_fault_region, write_fault, memory_calls, memory_fault_call;
static uint32_t read_fault_reg, run_readback_reg;
static unsigned run_read_fault, run_read_count;
static bool isolation_claimed;
static int isolation_error, isolation_calls, isolation_fault_call;
static unsigned memory_writes, memory_write_fault, memory_readback_fault;
static unsigned memory_change_call, cold_change_call, memory_map_fault;
static unsigned memory_order[4];
static bool cold_test, peer_power_on, cold_flush_clear;
static int peer_power_error;
static unsigned cold_writes, cold_fault_write, cold_readback_write;
static unsigned cold_change_write, cold_flush_masters;
static uint64_t cold_flush_time;
static unsigned cold_global_clear_phase, cold_global_deasserts;
static uint32_t cold_global_bad;
static bool cold_global_pending;
static uint32_t cold_write_regs[64], cold_write_values[64];
static uint64_t sequence;
static unsigned event_polls, event_limit;
static uint32_t corrupt_copy_reg;
static uint32_t de[0x400000/4], iommu[0xe40/4], nsi[0x420/4], clocks[2][7];
static struct clk clock_de0;
static const void *lease_owner;
static uint32_t power_cells[2], iommu_cells[3], nsi_cells[2];
static const char *iommu_status, *nsi_status;
static int cell_override, property_length, translated, bad_base, wrong_compatible;
static unsigned clock_puts;
static void *active_softc;

static void getnanouptime(struct timespec *t) { tick+=1000;t->tv_sec=tick/1000000000;t->tv_nsec=tick%1000000000; }
static void delay(unsigned us) { tick+=(uint64_t)us*1000; }
static int splhigh(void) { CHECK(!irq_masked);irq_masked=true;return 42; }
static void splx(int level) { CHECK(level==42&&irq_masked);irq_masked=false; }
static void mutex_init(kmutex_t *m,int kind,int ipl) { (void)kind;(void)ipl;*m=0; }
static void mutex_enter(kmutex_t *m) { CHECK(!*m);*m=1; }
static void mutex_exit(kmutex_t *m) { CHECK(*m);*m=0; }
static bool mutex_owned(kmutex_t *m) { return *m != 0; }
static bool mutex_tryenter(kmutex_t *m) { if (*m)return false;*m=1;return true; }
static void *device_private(device_t d) { return d; }
static const char *device_xname(device_t d) { (void)d;return "de3520"; }
static void aprint_naive(const char *s) { (void)s; }
static void aprint_normal(const char *s) { (void)s; }
static int sysctl_lookup(SYSCTLFN_ARGS) { CHECK(rnode->sysctl_size>0);if(newp){CHECK(rnode->sysctl_size==sizeof(u_int));memcpy(rnode->sysctl_data,newp,sizeof(u_int));}return 0; }
static int sysctl_createv(struct sysctllog **l,int f,const struct sysctlnode **p,
    const struct sysctlnode **out,int flags,int type,const char *name,const char *desc,
    int (*fn)(SYSCTLFN_ARGS),int value,void *data,size_t size,...)
{
 static struct sysctlnode n;
 (void)l;(void)f;(void)p;(void)flags;(void)type;(void)name;(void)desc;
 (void)fn;(void)value;(void)data;(void)size;if(out)*out=&n;return 0;
}
static int of_compatible_match(int node,const struct device_compatible_entry *table)
{
 const char *c=node==1?"allwinner,display-engine-v352":node==4?"allwinner,iommu-v20":"allwinner,sunxi-nsi-v2";
 if(wrong_compatible==node)return 0;
 return strcmp(c,table[0].compat)==0;
}
static int fdtbus_get_reg(int node,unsigned index,bus_addr_t *base,bus_size_t *size)
{
 CHECK(index==0);
 *base=node==1?0x05000000:node==4?0x03900000:0x02020000;
 *size=node==1?0x400000:node==4?0x20000:0x10000;
 if(bad_base==node)(*base)++;return 0;
}
static const void *fdtbus_get_prop(int node,const char *name,int *len)
{
 if(strcmp(name,"status")==0){const char *s=node==4?iommu_status:nsi_status;*len=s?(int)strlen(s)+1:0;return s;}
 CHECK(node==1);
 if(strcmp(name,"power-domains")==0){*len=8;return power_cells;}
 if(strcmp(name,"iommus")==0){*len=property_length?property_length:12;return iommu_cells;}
 if(strcmp(name,"nsi")==0){*len=8;return nsi_cells;}
 return NULL;
}
static int of_getprop_uint32(int node,const char *name,uint32_t *out)
{ (void)name;*out=cell_override?cell_override:node==4?2:1;return 0; }
static int fdtbus_get_phandle_from_native(uint32_t p) { return (int)p; }
static int fdtbus_get_phandle(int node,const char *name) { CHECK(node==1&&strcmp(name,"output")==0);return 7; }
static struct clk *fdtbus_clock_get_index(int node,unsigned i) { CHECK(node==1&&i==0);return &clock_de0; }
static void clk_put(struct clk *c) { CHECK(c==&clock_de0);clock_puts++; }
static int OF_parent(int node) { return node==1?2:node==2?3:0; }
static int OF_getproplen(int node,const char *name) { (void)name;return translated==node?24:-1; }
static bool of_hasprop(int node,const char *name) { (void)node;(void)name;return false; }
static int bus_space_map(bus_space_tag_t t,bus_addr_t a,bus_size_t n,int flags,bus_space_handle_t *h)
{ (void)t;(void)flags;CHECK((a==0x5000000&&n==0x400000)||(a==0x3910000&&n==0xe40)||(a==0x2020000&&n==0x420));*h=a==0x5000000?1:a==0x3910000?2:3;if(memory_map_fault==*h&&retained)return ENOMEM;space_maps++;return 0; }
static void bus_space_unmap(bus_space_tag_t t,bus_space_handle_t h,bus_size_t n)
{ (void)t;(void)n;CHECK(h>=1&&h<=3);space_unmaps++; }
static int bus_space_peek_4(bus_space_tag_t,bus_space_handle_t,bus_size_t,uint32_t *);
static int bus_space_poke_4(bus_space_tag_t,bus_space_handle_t,bus_size_t,uint32_t);
static void bus_space_barrier(bus_space_tag_t t,bus_space_handle_t h,bus_size_t off,bus_size_t n,int flags)
{ (void)t;(void)off;CHECK(h>=1&&h<=3&&(n==4||n==12)&&flags>0); }
static int sun60i_a733_pck_display_reserve(int power,const void *owner)
{ CHECK(power==6&&!power_claimed);if(power_error)return power_error;power_claimed=true;lease_owner=owner;return 0; }
static int sun60i_a733_pck_display_release(int power,const void *owner)
{ CHECK(power==6&&power_claimed&&!claimed&&!retained&&owner==lease_owner);power_releases++;if(pck_release_error)return pck_release_error;power_claimed=false;return 0; }
static int sun60i_a733_ccu_display_reserve(struct clk *c,int power,const void *owner)
{ CHECK(c==&clock_de0&&power==6&&!claimed&&power_claimed&&owner==lease_owner);if(reserve_error)return reserve_error;claimed=true;lease_owner=owner;return 0; }
static int sun60i_a733_ccu_display_memory_reserve(struct clk *c,const void *owner)
{ CHECK(c==&clock_de0&&claimed&&owner==lease_owner);if(memory_error)return memory_error;memory_claimed=true;return 0; }
int sun60i_a733_ccu_display_isolation_reserve(struct clk *c,const void *owner)
{ CHECK(c==&clock_de0&&claimed&&memory_claimed&&!retained&&owner==lease_owner);if(isolation_error)return isolation_error;isolation_claimed=true;return 0; }
int sun60i_a733_ccu_display_isolation_state(struct clk *c,const void *owner,uint32_t out[2][A733_DISPLAY_NISOLATION_REGS])
{ CHECK(c==&clock_de0&&isolation_claimed&&retained&&owner==lease_owner);isolation_calls++;if(isolation_calls==isolation_fault_call)return EBUSY;if((unsigned)isolation_calls==memory_change_call)nsi[0x414/4]^=8;if((unsigned)isolation_calls==cold_change_call)de[0x8000/4]=1;memset(out,0,2*A733_DISPLAY_NISOLATION_REGS*sizeof(uint32_t));return 0; }
static int sun60i_a733_ccu_display_begin_peer_access(struct clk *c,const void *owner)
{ CHECK(c==&clock_de0&&isolation_claimed&&retained&&owner==lease_owner);if(peer_power_error)return peer_power_error;peer_power_on=true;return 0; }
static int sun60i_a733_ccu_display_release(struct clk *c,const void *owner)
{ CHECK(c==&clock_de0&&claimed&&!retained&&owner==lease_owner);releases++;if(ccu_release_error)return ccu_release_error;claimed=memory_claimed=isolation_claimed=false;return 0; }
static int sun60i_a733_ccu_display_memory_state(struct clk *c,const void *owner,uint32_t out[2][7])
{ CHECK(c==&clock_de0&&claimed&&memory_claimed&&owner==lease_owner);memory_calls++;if(memory_calls==memory_fault_call)return EIO;memcpy(out,clocks,sizeof(clocks));return 0; }
static int sun60i_a733_ccu_display_begin_de0(struct clk *c,const void *owner,bool *attempted)
{ CHECK(c==&clock_de0&&claimed&&memory_claimed&&owner==lease_owner);begins++;*attempted=attempt_begin;retained=attempt_begin;return begin_error; }
static int sun60i_a733_ccu_display_ready(struct clk *c,const void *owner)
{ CHECK(c==&clock_de0&&claimed&&retained&&owner==lease_owner);return 0; }
static int sun60i_a733_ccu_display_fault(struct clk *c,const void *owner,int error)
{ CHECK(c==&clock_de0&&owner==lease_owner&&claimed&&retained&&error);ccu_faults++;return error; }
#include "sun60i_a733_output.h"
#include "sun60i_a733_hdmi_phy.h"
static struct sun60i_a733_output_mode mode={27000000,720,736,798,858,480,489,495,525,false,false};
int sun60i_a733_output_get_mode(int node,const void *owner,struct sun60i_a733_output_mode *out)
{ CHECK(node==7&&owner==lease_owner&&retained);if(mode_error)return mode_error;*out=mode;return 0; }
int sun60i_a733_output_prepare(int node,const void *owner,const struct sun60i_a733_output_mode *m)
{ CHECK(node==7&&owner==lease_owner&&retained&&m->hdisplay==720);return prepare_error; }
int sun60i_a733_output_start(int node,const void *owner)
{ CHECK(node==7&&owner==lease_owner&&retained&&(de[0x8100/4]&1));if(start_error)return start_error;started=true;if(!no_busy)de[0x8104/4]|=0x10;return 0; }
int sun60i_a733_output_observed_vblank(int node,const void *owner,uint64_t *out)
{ CHECK(node==7&&owner==lease_owner&&retained&&started);event_polls++;if(no_vblank||event_polls%2||(event_limit&&sequence>=event_limit))return ENOENT;if(!same_sequence)sequence++;*out=sequence;return 0; }
int sun60i_a733_output_fault(int node,const void *owner,int error)
{ CHECK(node==7&&owner==lease_owner&&retained&&error);faults++;return error; }
int
sun60i_a733_output_cached_trace(int node, struct sun60i_a733_output_trace *out)
{
	CHECK(node == 7);
	memset(out, 0, sizeof(*out));
	return 0;
}
int
sun60i_a733_output_cached_phy_trace(int node,
    struct sun60i_a733_hdmi_phy_trace *out)
{
	CHECK(node == 7);
	memset(out, 0, sizeof(*out));
	return 0;
}
static unsigned vblank_binds;
static int vblank_bind_error;
static sun60i_a733_vblank_fn bound_fn;
static void *bound_arg;
int
sun60i_a733_output_set_vblank_handler(int node, const void *owner,
    sun60i_a733_vblank_fn fn, void *arg)
{
	CHECK(node == 7 && owner == lease_owner && retained && started);
	vblank_binds++;
	if (vblank_bind_error == 0) {
		bound_fn = fn;
		bound_arg = arg;
	}
	return vblank_bind_error;
}
static void
vblank_consumer(void *arg, uint64_t sequence)
{
	(void)arg;
	(void)sequence;
}
static unsigned output_samples;
static int output_sample_error;
int
sun60i_a733_output_sample(int node, const void *owner,
    struct sun60i_a733_output_sample *out)
{
	CHECK(node == 7 && owner == lease_owner && retained && faults == 0);
	output_samples++;
	memset(out, 0, sizeof(*out));
	out->version = 1;
	out->valid = 0xff;
	out->words[6] = 10;
	out->words[7] = 74;
	return output_sample_error;
}
#include "sun60i_a733_de352_scanout.h"
#include "sun60i_a733_de352_fdt.c"

static int bus_space_peek_4(bus_space_tag_t tag,bus_space_handle_t h,bus_size_t off,uint32_t *out)
{
 (void)tag;reads++;CHECK((off&3)==0);
 if ((int)h == 1 && ((struct sun60i_de352_softc *)active_softc)->step == 7 &&
     off == 0x8050 && ++run_read_count == run_read_fault)
  return EIO;
 if((int)h==read_fault_region&&off==read_fault_reg)return EIO;
 if(h==1){CHECK(retained&&off<sizeof(de));*out=de[off/4];}
 else if(h==2){CHECK(memory_claimed&&off<sizeof(iommu));
  if(cold_test&&off>=0x400&&off<0x800)CHECK(peer_power_on);
  if(cold_test&&off==0x30&&cold_global_pending){
   const struct sun60i_de352_softc *sc=active_softc;
   if((cold_global_clear_phase==2&&sc->cold_trace.phase==CT_STEP_CAPTURE)||
      (cold_global_clear_phase==3&&sc->cold_trace.phase==CT_BEFORE)||
      (cold_global_clear_phase==5&&sc->cold_trace.phase==CT_STEP_CAPTURE&&sc->cold_trace.capture_words>CI_COUNT))iommu[off/4]=0;
   if(cold_global_clear_phase==4&&sc->cold_trace.phase==CT_STEP_CAPTURE)iommu[off/4]=3;
  }
  *out=iommu[off/4];}
 else {CHECK(h==3&&memory_claimed&&off<sizeof(nsi));*out=nsi[off/4];}
 return 0;
}
static int bus_space_poke_4(bus_space_tag_t tag,bus_space_handle_t h,bus_size_t off,uint32_t value)
{
 struct sun60i_de352_softc *sc=active_softc;
 struct sun60i_a733_de352_dma *dma=sc->pending.dma?sc->pending.dma:sc->dma;
 (void)tag;
 if(cold_test&&h==2){
  CHECK(retained&&claimed&&memory_claimed&&isolation_claimed&&peer_power_on);
  CHECK(sc->dma==NULL&&de[0x8004/4]==0&&(de[0x8000/4]&1)==0);
  CHECK(sc->guard&&sc->guard->published&&sc->guard->directory.map->prewrites==1);
  CHECK(off!=0x84&&off!=0x1c);
  CHECK(cold_writes<__arraycount(cold_write_regs));
  if(off==0x30&&value==3){cold_flush_time=tick;cold_global_pending=true;}
  if(off==0x30&&value==0){
   CHECK(cold_global_pending&&cold_flush_masters==0x3f&&tick-cold_flush_time>=10000);
   cold_global_deasserts++;cold_global_pending=false;
  }
  if(off>=0x400&&(off-0x400)%0x200==0x18)cold_flush_masters|=1U<<((off-0x400)/0x200);
  if(off==0x14){CHECK(value==1&&cold_flush_masters==0x3f&&iommu[0x30/4]==0);CHECK(tick-cold_flush_time>=10000);}
  cold_write_regs[cold_writes]=(uint32_t)off;cold_write_values[cold_writes]=value;cold_writes++;
  if(cold_writes==cold_fault_write)return EIO;
  bool mic_flush=off>=0x400&&(off-0x400)%0x200==0x18;
  if(cold_writes!=cold_readback_write){
   iommu[off/4]=cold_flush_clear&&mic_flush?0:value;
   if(off==0x30&&value==3){
    if(cold_global_clear_phase==1||cold_global_clear_phase==4)iommu[off/4]=0;
    if(cold_global_bad)iommu[off/4]=cold_global_bad;
   }
  }else if(mic_flush)iommu[off/4]=2; /* Invalid readback, not a permitted self-clear. */
  else if(off==0x30&&value==3)iommu[off/4]=4; /* Zero is an observed request state. */
  if(cold_writes==cold_change_write)iommu[0x88/4]=1;
  return 0;
 }
 if(h==2||h==3){
  CHECK(retained&&claimed&&memory_claimed&&isolation_claimed&&sc->dma==NULL&&de[0x8004/4]==0&&(de[0x8000/4]&1)==0);
  CHECK(iommu[0]==0x16&&iommu[0x14/4]==1&&iommu[0x838/4]==0);
  CHECK((h==2&&(off==0x804||off==0x10))||(h==3&&(off==0x410||off==0x418)));
  CHECK(memory_writes<4);memory_order[memory_writes]=(h<<16)|(unsigned)off;memory_writes++;
  if(memory_writes==memory_write_fault)return EIO;
  if(memory_writes!=memory_readback_fault)(h==2?iommu:nsi)[off/4]=value;
  return 0;
 }
 CHECK(h==1&&retained&&off<sizeof(de));writes++;
 if(off>=0x8114&&off<=0x811c)CHECK(dma&&dma->published&&dma->synchronized&&syncs>=2);
 if((int)off==write_fault)return EIO;
 if(off==0x8104){if(!bad_w1c)de[off/4]&=~(value&0xd);return 0;}
 if(off==0x8110){
  CHECK(irq_masked&&started&&dma->published&&(de[0x8104/4]&0x10)&&value==1);
  CHECK(de[0x8114/4]==dma->queue.address&&de[0x8118/4]==0&&de[0x811c/4]==32);
  de[off/4]=0;
  if(no_finish)return 0;
  if(!no_copy)for(unsigned i=0;i<2;i++){
   uint8_t *d=(uint8_t *)dma->queue.cpu+i*16;
   uint32_t src=get32(d),n=get32(d+4)&0xffffff,dst=get32(d+12);
   CHECK(src>=dma->queue.address&&src+n<=dma->queue.address+dma->queue.bytes&&dst+n<=sizeof(de));
   for(unsigned j=0;j<n;j+=4)de[(dst+j)/4]=get32((uint8_t *)dma->queue.cpu+(src-dma->queue.address)+j);
  }
  if(corrupt_copy_reg)de[corrupt_copy_reg/4]^=1;
  de[0x8104/4]|=4;return 0;
 }
 if (sc->step == 7 && off == run_readback_reg)
  value ^= 1;
 de[off/4]=value;return 0;
}
static void init(struct sun60i_de352_softc *sc)
{
 reset();de352_provider=NULL;memset(sc,0,sizeof(*sc));memset(de,0,sizeof(de));memset(iommu,0,sizeof(iommu));memset(nsi,0,sizeof(nsi));
 tick=0;reads=writes=space_maps=space_unmaps=releases=faults=begins=0;
 power_error=power_releases=ccu_release_error=pck_release_error=ccu_faults=0;
 reserve_error=memory_error=begin_error=mode_error=prepare_error=start_error=0;
 attempt_begin=true;claimed=memory_claimed=retained=started=irq_masked=power_claimed=false;lease_owner=NULL;
 isolation_claimed=false;isolation_error=isolation_calls=isolation_fault_call=0;
 cold_flush_masters=0;cold_flush_time=0;
 cold_global_clear_phase=cold_global_deasserts=cold_global_bad=0;cold_global_pending=false;
 cold_test=peer_power_on=cold_flush_clear=false;peer_power_error=0;cold_writes=cold_fault_write=cold_readback_write=cold_change_write=0;
 memory_writes=memory_write_fault=memory_readback_fault=memory_change_call=cold_change_call=memory_map_fault=0;memset(memory_order,0,sizeof(memory_order));
 no_busy=no_finish=no_copy=no_vblank=same_sequence=bad_w1c=false;
 output_samples=0;output_sample_error=0;
 run_readback_reg=run_read_fault=run_read_count=0;
 read_fault_region=write_fault=memory_calls=memory_fault_call=0;read_fault_reg=0;sequence=0;event_polls=event_limit=corrupt_copy_reg=0;
 cell_override=property_length=translated=bad_base=wrong_compatible=0;clock_puts=0;
 iommu_status=nsi_status="disabled";
 power_cells[0]=be32toh(6);power_cells[1]=be32toh(1);
 iommu_cells[0]=be32toh(4);iommu_cells[1]=be32toh(8);iommu_cells[2]=be32toh(1);
 nsi_cells[0]=be32toh(5);nsi_cells[1]=be32toh(2);
 uint32_t c[7]={0x10007,0x10001,0x81000000,0x802,0x02000200,0x02000000,0xc3000000};
 memcpy(clocks[0],c,sizeof(c));memcpy(clocks[1],c,sizeof(c));
 iommu[0]=0x16;iommu[0x10/4]=4;iommu[0x14/4]=1;iommu[0x804/4]=1;nsi[0x418/4]=1;
 sc->valid=true;sc->phandle=1;sc->power_phandle=6;sc->output_phandle=7;sc->de0=&clock_de0;sc->dmat=(void *)(uintptr_t)2;active_softc=sc;
}
static void reboot_fixture(struct sun60i_de352_softc *sc)
{
 if(sc->guard){sc->guard->published=false;CHECK(sun60i_a733_de352_guard_destroy(sc->guard)==0);sc->guard=NULL;}

 /* The fixture models a reset after asserting retention. Production cannot do this. */
 struct de352_frame *frames[]={&sc->active,&sc->pending};
 for(unsigned i=0;i<2;i++)if(frames[i]->dma){frames[i]->dma->published=false;de352_frame_drop(frames[i],false);memset(frames[i],0,sizeof(*frames[i]));}
 if(sc->dma){sc->dma->published=false;CHECK(sun60i_a733_de352_dma_destroy(sc->dma)==0);sc->dma=NULL;}
 reset();
}
struct frame_fixture {
 struct dma_map map;
 unsigned held, retains, drops;
 int error;
 bool reenter;
};
static int frame_retain(void *arg)
{
 struct frame_fixture *f=arg;struct sun60i_de352_softc *sc=active_softc;
 CHECK(!mutex_owned(&sc->lock));f->retains++;
 if(f->reenter){
  struct sun60i_a733_de352_state state;
  struct sun60i_a733_de352_completion result;
  struct sun60i_a733_de352_frame frame={sc->dma->tag,&f->map,2880*480,
   720,480,2880,f,frame_retain,sc->active.info.release};
  CHECK(sun60i_a733_de352_get_state(1,&state)==0);
  CHECK(sun60i_a733_de352_release(&sc->client)==EBUSY);
  CHECK(sun60i_a733_de352_submit(&sc->client,&frame,&result)==EBUSY);
 }
 if(f->error)return f->error;
 f->held++;return 0;
}
static void frame_release(void *arg)
{
 struct frame_fixture *f=arg;struct sun60i_de352_softc *sc=active_softc;
 CHECK(!mutex_owned(&sc->lock)&&f->held>0);f->held--;f->drops++;
}
static struct sun60i_a733_de352_frame frame_input(struct frame_fixture *f,
    bus_dma_tag_t tag, uint64_t address)
{
 memset(f,0,sizeof(*f));f->map.dm_nsegs=1;f->map.dm_mapsize=2880*480;
 f->map.dm_segs[0]=(bus_dma_segment_t){address,2880*480};
 return (struct sun60i_a733_de352_frame){tag,&f->map,2880*480,
  720,480,2880,f,frame_retain,frame_release};
}
static void scanout_checks(void)
{
 struct sun60i_de352_softc sc;struct sun60i_a733_de352_state state,canary;
 struct sun60i_a733_de352_client *client=(void *)(uintptr_t)1;
 struct sun60i_a733_de352_completion done,old_done;
 struct frame_fixture f[3];struct sun60i_a733_de352_frame input[3];
 init(&sc);memset(&state,0xa5,sizeof(state));canary=state;
 CHECK(sun60i_a733_de352_get_state(1,&state)==ENXIO);
 de352_provider=&sc;CHECK(sun60i_a733_de352_get_state(1,&state)==EAGAIN);
 CHECK(memcmp(&state,&canary,sizeof(state))==0&&reads==0&&writes==0);
 CHECK(sun60i_a733_de352_acquire(1,&sc,&client)==EAGAIN&&client==(void *)(uintptr_t)1);
 CHECK(de352_run(&sc)==0);sc.stage=1;
 CHECK(sun60i_a733_de352_get_state(1,&state)==0&&state.dmat==sc.dma->tag&&state.max_dma_address==UINT32_MAX);
 CHECK(sun60i_a733_de352_acquire(1,&sc,&client)==0);
 CHECK(sun60i_a733_de352_acquire(1,&state,&client)==EBUSY);
 CHECK(sun60i_a733_de352_release(client)==0);
 CHECK(sun60i_a733_de352_set_vblank_handler(client,vblank_consumer,&state)==EINVAL&&vblank_binds==0);
 CHECK(sun60i_a733_de352_acquire(1,&sc,&client)==0);
 CHECK(sun60i_a733_de352_set_vblank_handler(NULL,vblank_consumer,&state)==EINVAL);
 CHECK(sun60i_a733_de352_set_vblank_handler(client,vblank_consumer,NULL)==EINVAL&&vblank_binds==0);
 CHECK(sun60i_a733_de352_set_vblank_handler(client,vblank_consumer,&state)==0);
 CHECK(vblank_binds==1&&bound_fn==vblank_consumer&&bound_arg==&state&&!mutex_owned(&sc.lock));
 vblank_bind_error=EOPNOTSUPP;
 CHECK(sun60i_a733_de352_set_vblank_handler(client,vblank_consumer,&state)==EOPNOTSUPP);
 vblank_bind_error=0;
 CHECK(sun60i_a733_de352_set_vblank_handler(client,NULL,NULL)==0&&bound_fn==NULL&&bound_arg==NULL);
 for(unsigned i=0;i<3;i++)input[i]=frame_input(&f[i],state.dmat,0x20000000+(uint64_t)i*0x1000000);
 memset(&done,0xa5,sizeof(done));old_done=done;
 input[0].dmat=sc.dmat;int previous_writes=writes;
 CHECK(sun60i_a733_de352_submit(client,&input[0],&done)==EINVAL&&writes==previous_writes&&f[0].retains==0);
 input[0].dmat=state.dmat;f[0].error=ENOMEM;
 CHECK(sun60i_a733_de352_submit(client,&input[0],&done)==ENOMEM&&f[0].held==0&&f[0].drops==0&&sc.result==0);
 CHECK(memcmp(&done,&old_done,sizeof(done))==0);f[0].error=0;
 CHECK(sun60i_a733_de352_submit(client,&input[0],&done)==0);
 CHECK(f[0].held==1&&f[0].drops==0&&sc.active.dma&&sc.pending.dma==NULL);
 CHECK(done.observed_sequence>=4&&done.observed_time_ns>0&&sc.vblank==done.observed_sequence);
 CHECK(sun60i_a733_de352_release(client)==EBUSY);
 event_polls++;uint64_t before_sequence=sequence;
 f[1].reenter=true;CHECK(sun60i_a733_de352_submit(client,&input[1],&done)==0);
 CHECK(done.observed_sequence==before_sequence+3);
 CHECK(f[0].held==0&&f[0].drops==1&&f[1].held==1&&posts==2);
 write_fault=0x8114;old_done=done;
 CHECK(sun60i_a733_de352_submit(client,&input[2],&done)==EIO);
 CHECK(f[1].held==1&&f[2].held==1&&sc.active.dma&&sc.pending.dma&&sc.pending.dma->published);
 CHECK(memcmp(&done,&old_done,sizeof(done))==0&&posts==2&&sc.result==EIO);
 CHECK(sun60i_a733_de352_release(client)==EBUSY);
 CHECK(sun60i_a733_de352_submit(client,&input[0],&done)==EIO);
 reboot_fixture(&sc);CHECK(f[1].held==0&&f[2].held==0);
 /* Each new failure starts after a real accepted active external frame. */
 for(unsigned fault=0;fault<8;fault++){
  init(&sc);de352_provider=&sc;CHECK(de352_run(&sc)==0);sc.stage=1;
  CHECK(sun60i_a733_de352_get_state(1,&state)==0);
  CHECK(sun60i_a733_de352_acquire(1,&sc,&client)==0);
  for(unsigned i=0;i<2;i++)input[i]=frame_input(&f[i],state.dmat,0x20000000+(uint64_t)i*0x1000000);
  CHECK(sun60i_a733_de352_submit(client,&input[0],&done)==0);
  old_done=done;previous_writes=writes;int expected=0;
  switch(fault){
  case 0: fail_nosleep=true;expected=ENOMEM;break;
  case 1: de[0x8114/4]^=0x1000;expected=EBUSY;break;
  case 2: no_vblank=true;expected=ETIMEDOUT;break;
  case 3: same_sequence=true;expected=EPROTO;break;
  case 4: corrupt_copy_reg=0x1c100c;expected=EIO;break;
  case 5: event_limit=(unsigned)sequence+1;expected=ETIMEDOUT;break;
  case 6: memory_fault_call=memory_calls+2;expected=EIO;break;
  case 7: no_finish=true;expected=ETIMEDOUT;break;
  }
  CHECK(sun60i_a733_de352_submit(client,&input[1],&done)==expected);
  CHECK(memcmp(&done,&old_done,sizeof(done))==0&&f[0].held==1&&f[0].drops==0&&posts==0);
  if(fault<2){
   CHECK(writes==previous_writes&&f[1].held==0&&f[1].drops==1&&sc.pending.dma==NULL);
   CHECK(sc.result==(fault==0?0:expected));
  }else{
   CHECK(f[1].held==1&&f[1].drops==0&&sc.pending.dma&&sc.pending.dma->published&&sc.result==expected);
  }
  reboot_fixture(&sc);CHECK(f[0].held==0&&f[1].held==0);
 }
}
static void
managed_init(struct sun60i_de352_softc *sc)
{
 init(sc);iommu[0x10/4]=0x20;iommu[0x804/4]=0;
 nsi[0x410/4]=0x1003;nsi[0x414/4]=0x880;nsi[0x418/4]=0x100;
}
static void
managed_checks(void)
{
 struct sun60i_de352_softc sc;
 managed_init(&sc);CHECK(de352_run(&sc)==0);
 CHECK(memory_writes==4&&memory_order[0]==0x20804&&memory_order[1]==0x30410&&memory_order[2]==0x30418&&memory_order[3]==0x20010);
 CHECK(iommu[0x10/4]==0x24&&iommu[0x804/4]==1&&nsi[0x410/4]==0x1000&&nsi[0x414/4]==0x880&&nsi[0x418/4]==0x101);
 CHECK(isolation_claimed&&sc.retained&&sc.dma&&sc.dma->published&&space_maps==space_unmaps+1);reboot_fixture(&sc);
 for(unsigned i=0;i<4;i++){
  init(&sc);if(i==0)iommu[0x10/4]=0;if(i==1)iommu[0x804/4]=0;if(i==2)nsi[0x410/4]=3;if(i==3)nsi[0x418/4]=0;
  CHECK(de352_run(&sc)==0&&memory_writes==1);reboot_fixture(&sc);
 }
 managed_init(&sc);isolation_error=EBUSY;CHECK(de352_run(&sc)==EBUSY&&!sc.retained&&begins==0&&writes==0&&memory_writes==0&&releases==1&&power_releases==1);
 for(unsigned i=1;i<=4;i++){
  managed_init(&sc);memory_write_fault=i;CHECK(de352_run(&sc)==EIO&&memory_writes==i&&writes==0&&sc.retained&&sc.dma==NULL&&releases==0&&ccu_faults==1);
  managed_init(&sc);memory_readback_fault=i;CHECK(de352_run(&sc)==EIO&&memory_writes==i&&writes==0&&sc.retained&&sc.dma==NULL&&releases==0&&ccu_faults==1);
 }
 for(unsigned i=1;i<=6;i++){
  managed_init(&sc);isolation_fault_call=i;CHECK(de352_run(&sc)==EBUSY&&memory_writes==(i<=2?0:i-2)&&writes==0&&sc.retained&&sc.dma==NULL);
  managed_init(&sc);cold_change_call=i;CHECK(de352_run(&sc)==EBUSY&&memory_writes==(i<=2?0:i-2)&&writes==0&&sc.retained&&sc.dma==NULL);
 }
 for(unsigned i=2;i<=6;i++){
  managed_init(&sc);memory_change_call=i;CHECK(de352_run(&sc)==EBUSY&&memory_writes==i-2&&writes==0&&sc.retained&&sc.dma==NULL);
 }
 for(unsigned i=2;i<=3;i++){
  managed_init(&sc);memory_map_fault=i;CHECK(de352_run(&sc)==ENOMEM&&memory_writes==0&&writes==0&&sc.retained&&space_maps==space_unmaps+1);
 }
 managed_init(&sc);de[0x8004/4]=1;CHECK(de352_run(&sc)==EBUSY&&memory_writes==0&&writes==0&&sc.retained);
 managed_init(&sc);de[0x8000/4]=1;CHECK(de352_run(&sc)==EBUSY&&memory_writes==0&&writes==0&&sc.retained);
 managed_init(&sc);iommu[0x14/4]=2;CHECK(de352_run(&sc)==EOPNOTSUPP&&begins==0&&memory_writes==0&&writes==0&&!sc.retained);
}
static void cold_init(struct sun60i_de352_softc *sc) {
 init(sc);cold_test=true;memset(iommu,0,sizeof(iommu));iommu[0]=0x16;
 iommu[0x10/4]=0xffffffc0; /* Preserve all unrelated reset bits. */
 iommu[0x2c/4]=0x3007f;iommu[0x80/4]=3;
 for(unsigned i=0;i<6;i++)iommu[(0x400+i*0x200+0x30)/4]=1;
}
static void cold_checks(void) {
 struct sun60i_de352_softc sc;unsigned count;
 cold_init(&sc);CHECK(de352_run(&sc)==0&&sc.cold_initialized&&sc.guard->published);
 CHECK(iommu[0x14/4]==1&&iommu[0x10/4]==UINT32_MAX&&iommu[0x1c/4]==0);
 CHECK(iommu[0x20/4]==sc.guard->directory.address>>14);
 for(unsigned i=0;i<16384;i++)CHECK(((unsigned char *)sc.guard->directory.cpu)[i]==0);
 CHECK(sun60i_a733_de352_guard_destroy(sc.guard)==EBUSY);
 CHECK(sequence>=2&&started&&sc.dma->published);
 CHECK(sc.cold_trace.version==1&&sc.cold_trace.phase==CT_DONE&&sc.cold_trace.error==0);
 CHECK(sc.cold_trace.op==UINT32_MAX&&sc.cold_trace.valid==0);
 CHECK(sc.cold_trace.capture_attempt==31&&sc.cold_trace.capture_generation==31);
 CHECK(sc.cold_trace.capture_words==2*CI_COUNT&&sc.cold_trace.capture_valid==7);
 CHECK(memcmp(&sc.cold_trace.snapshot,&sc.cold,sizeof(sc.cold))==0);
 static const uint32_t expected_regs[]={0x80,0x430,0x630,0x830,0xa30,0xc30,0xe30,
  0x404,0x604,0x804,0xa04,0xc04,0xe04,0x20,0x2c,0x40,0x60,0x10,0x30,
  0x418,0x618,0x818,0xa18,0xc18,0xe18,0x30,0x14};
 static const uint32_t expected_values[]={0,0,0,0,0,0,0,1,1,1,1,1,1,0xc000,0,1,1,UINT32_MAX,3,1,1,1,1,1,1,0,1};
 count=cold_writes;CHECK(count==__arraycount(expected_regs));
 CHECK(memcmp(cold_write_regs,expected_regs,sizeof(expected_regs))==0);
 CHECK(memcmp(cold_write_values,expected_values,sizeof(expected_values))==0);
 for(unsigned i=0;i<6;i++){CHECK(iommu[(0x404+i*0x200)/4]==1);CHECK(iommu[(0x430+i*0x200)/4]==0);}
 CHECK(de352_memory(&sc)==0);
 iommu[0xe38/4]=1;CHECK(de352_memory(&sc)==EBUSY);
 reboot_fixture(&sc);
 for(unsigned fault=1;fault<=count;fault++)for(unsigned kind=0;kind<3;kind++){
  cold_init(&sc);if(kind==0)cold_fault_write=fault;if(kind==1)cold_readback_write=fault;if(kind==2)cold_change_write=fault;
  int result=de352_run(&sc);
  if(result==0||!sc.retained||!sc.guard||!sc.guard->published||sc.dma!=NULL)fprintf(stderr,"cold fault=%u kind=%u result=%d writes=%u\n",fault,kind,result,cold_writes);
  CHECK(result!=0&&sc.retained&&sc.guard&&sc.guard->published&&sc.dma==NULL);
  CHECK(cold_writes==fault&&writes==0&&!started&&ccu_faults==1&&releases==0);
  CHECK(sc.cold_trace.error==(uint32_t)result&&sc.cold_trace.capture_valid==7);
  if(kind<2){
   CHECK(sc.cold_trace.reg==expected_regs[fault-1]);
   CHECK(sc.cold_trace.requested==expected_values[fault-1]);
   CHECK(sc.cold_trace.phase==(kind==0?CT_WRITE:CT_AFTER));
   CHECK(sc.cold_trace.valid==(kind==0?7U:15U));
   if(kind==1)CHECK(sc.cold_trace.after!=sc.cold_trace.requested);
  }else CHECK(sc.cold_trace.phase==CT_STEP_COMPARE||sc.cold_trace.phase==CT_FINAL_COMPARE);
  reboot_fixture(&sc);
 }
 for(unsigned i=0;i<CI_COUNT;i++){
  cold_init(&sc);uint32_t reg=de352_cold_reg(i);
  if(i==CI_RESET)continue;
  if(i==CI_VERSION||i==CI_ENABLE)iommu[reg/4]=2;
  else if(i==CI_AUTOGATE)iommu[reg/4]=1;
  else if(i==CI_TTB)iommu[reg/4]=0x40000;
  else if(i==CI_PREFETCH)iommu[reg/4]=0x80000000;
  else if(i==CI_TLB_MODE||i==CI_PTW_MODE)iommu[reg/4]=2;
  else if(i==CI_IRQ_MASK)iommu[reg/4]=4;
  else if(i>=CI_MIC_BASE&&(i-CI_MIC_BASE)%4==0)iommu[reg/4]=2;
  else if(i>=CI_MIC_BASE&&(i-CI_MIC_BASE)%4==2)iommu[reg/4]=2;
  else iommu[reg/4]=1;
  CHECK(de352_run(&sc)!=0&&cold_writes==0&&writes==0&&sc.guard==NULL);
  reboot_fixture(&sc);
 }
 for(unsigned i=0;i<CI_COUNT;i++){
  cold_init(&sc);read_fault_region=2;read_fault_reg=de352_cold_reg(i);
  CHECK(de352_run(&sc)==EIO&&cold_writes==0&&writes==0&&sc.guard==NULL);reboot_fixture(&sc);
 }
 cold_init(&sc);peer_power_error=ETIMEDOUT;
 CHECK(de352_run(&sc)==ETIMEDOUT&&!peer_power_on&&cold_writes==0&&sc.guard==NULL);
 CHECK(sc.cold_trace.phase==CT_PEER_ACCESS&&sc.cold_trace.error==ETIMEDOUT);
 CHECK(sc.cold_trace.op==UINT32_MAX&&sc.cold_trace.valid==0&&sc.cold_trace.capture_attempt==0);
 CHECK(sc.cold_trace.capture_valid==0&&sc.cold_trace.capture_generation==0);
 reboot_fixture(&sc);
 cold_init(&sc);iommu[0x1c/4]=0x7fff0003;
 CHECK(de352_run(&sc)==0&&iommu[0x1c/4]==0x7fff0003);reboot_fixture(&sc);
 cold_init(&sc);cold_flush_clear=true;
 CHECK(de352_run(&sc)==0&&sc.cold_initialized);reboot_fixture(&sc);
 for(int f=1;f<=5;f++){
  cold_init(&sc);fail_call=f;
  CHECK(de352_run(&sc)==EIO&&cold_writes==0&&sc.guard==NULL);reboot_fixture(&sc);
 }
}
/* Global request readback is state only. Every path must still deassert it. */
static void cold_global_checks(void) {
 struct sun60i_de352_softc sc;
 for(unsigned phase=0;phase<4;phase++){
  cold_init(&sc);cold_global_clear_phase=phase;
  iommu[0x10/4]=0x37fff;iommu[0x804/4]=1;
  int error=de352_run(&sc);
  if(error!=0)fprintf(stderr,"global phase=%u error=%d step=%d op=%u reg=%#x requested=%u after=%u\n",phase,error,sc.step,sc.cold_trace.op,sc.cold_trace.reg,sc.cold_trace.requested,sc.cold_trace.after);
  CHECK(error==0&&sc.cold_initialized&&sc.step==10);
  CHECK(cold_global_deasserts==1&&cold_flush_masters==0x3f&&!cold_global_pending);
  CHECK(iommu[0x30/4]==0&&iommu[0x14/4]==1&&sc.guard->published&&sc.dma->published);
  CHECK(sc.cold_trace.phase==CT_DONE&&sc.cold_trace.error==0);
  unsigned globals=0;
  for(unsigned i=0;i<cold_writes;i++)if(cold_write_regs[i]==0x30){
   CHECK(globals<2&&cold_write_values[i]==(globals==0?3U:0U));globals++;
  }
  CHECK(globals==2);reboot_fixture(&sc);
 }
 static const uint32_t malformed[]={1,2,4,0x80000000,UINT32_MAX};
 for(unsigned i=0;i<__arraycount(malformed);i++){
  cold_init(&sc);cold_global_bad=malformed[i];
  CHECK(de352_run(&sc)==EIO&&sc.step==4&&!sc.cold_initialized);
  CHECK(sc.cold_trace.phase==CT_AFTER&&sc.cold_trace.op==19&&sc.cold_trace.valid==15);
  CHECK(sc.cold_trace.reg==0x30&&sc.cold_trace.before==0&&sc.cold_trace.requested==3&&sc.cold_trace.after==malformed[i]);
  CHECK(sc.guard&&sc.guard->published&&sc.retained&&sc.dma==NULL&&writes==0);
  CHECK(cold_flush_masters==0&&cold_global_deasserts==0&&releases==0);
  reboot_fixture(&sc);
 }
 for(unsigned phase=4;phase<=5;phase++){
  cold_init(&sc);cold_global_clear_phase=phase;
  CHECK(de352_run(&sc)==EBUSY&&sc.step==4&&!sc.cold_initialized);
  CHECK(sc.cold_trace.op==20&&sc.cold_trace.phase==(phase==4?CT_STEP_COMPARE:CT_STEP_CAPTURE));
  CHECK(sc.guard&&sc.guard->published&&sc.retained&&sc.dma==NULL&&writes==0);
  CHECK(cold_flush_masters==0&&cold_global_deasserts==0&&releases==0);
  reboot_fixture(&sc);
 }
 /* Only an outstanding full request can fall to zero in a later snapshot. */
 for(unsigned prior=0;prior<=4;prior++)for(unsigned after=0;after<=4;after++){
  struct de352_cold_state expected={0},actual={0};
  expected.words[0][CI_FLUSH]=expected.words[1][CI_FLUSH]=prior;
  actual.words[0][CI_FLUSH]=actual.words[1][CI_FLUSH]=after;
  struct de352_cold_state saved=expected;
  int error=de352_cold_same(&expected,&actual);
  bool accepted=prior==after||(prior==3&&after==0);
  CHECK(error==(accepted?0:EBUSY));
  CHECK(memcmp(&expected,accepted?&actual:&saved,sizeof(expected))==0);
 }
}
/* A failed new capture cannot publish uninitialized partial words. */
static void cold_trace_checks(void) {
 struct sun60i_de352_softc sc;struct de352_cold_state out, saved;
 struct de352_cold_trace trace={0};
 cold_init(&sc);CHECK(de352_run(&sc)==0);
 CHECK(de352_cold_read(&sc,2,&out,&trace)==0);
 CHECK(trace.capture_attempt==1&&trace.capture_generation==1&&trace.capture_valid==7);
 CHECK(trace.capture_words==2*CI_COUNT);saved=trace.snapshot;
 memset(&out,0xa5,sizeof(out));struct de352_cold_state sentinel=out;
 read_fault_region=2;read_fault_reg=0xe38;
 CHECK(de352_cold_read(&sc,2,&out,&trace)==EIO);
 CHECK(trace.capture_attempt==2&&trace.capture_generation==1&&trace.capture_valid==7);
 CHECK(trace.capture_words==CI_COUNT-1&&trace.capture_reg==0xe38);
 CHECK(memcmp(&trace.snapshot,&saved,sizeof(saved))==0&&memcmp(&out,&sentinel,sizeof(out))==0);
 struct sysctlnode node={&sc,sizeof(sc.cold_trace)};
 struct de352_cold_trace before=sc.cold_trace;
 int old_reads=reads,old_writes=writes,old_maps=space_maps;
 CHECK(de352_sysctl_cold_trace(&node,NULL)==0);
 CHECK(de352_sysctl_cold_trace(&node,&sc)==EROFS);
 CHECK(reads==old_reads&&writes==old_writes&&space_maps==old_maps);
 CHECK(memcmp(&sc.cold_trace,&before,sizeof(before))==0);
 reboot_fixture(&sc);
}
/* Released MIC reset bits with a bypass that is already set. */
static void cold_reset_checks(void) {
 struct sun60i_de352_softc sc;
 cold_init(&sc);iommu[0x10/4]=0x37fff;iommu[0x804/4]=1;
 CHECK(de352_run(&sc)==0&&sc.cold_initialized&&sc.step==10);
 CHECK(cold_write_regs[0]==0x10&&cold_write_values[0]==0x37fc0);
 CHECK(iommu[0x10/4]==0x37fff&&iommu[0x804/4]==1);
 for(unsigned i=0;i<cold_writes;i++)CHECK(cold_write_regs[i]!=0x804);
 reboot_fixture(&sc);
 for(unsigned kind=0;kind<3;kind++){
  cold_init(&sc);iommu[0x10/4]=0x37fff;iommu[0x804/4]=1;
  if(kind==0)cold_fault_write=1;if(kind==1)cold_readback_write=1;if(kind==2)cold_change_write=1;
  CHECK(de352_run(&sc)!=0&&sc.retained&&sc.guard&&sc.guard->published&&sc.dma==NULL);
  CHECK(cold_writes==1&&cold_write_regs[0]==0x10&&writes==0&&!started&&ccu_faults==1&&releases==0);
  reboot_fixture(&sc);
 }
}
static void
run_trace_checks(void)
{
	static const uint32_t regs[] = { 0x8050, 0x810c, 0x8100 };
	static const uint32_t phases[] = {
		RT_CONFIGURE, RT_MIXER_ENABLE, RT_DE_ENABLE
	};
	struct sun60i_de352_softc sc;
	struct de352_run_trace saved;
	int old_reads, old_writes, old_maps;

	init(&sc);
	prepare_error = EIO;
	CHECK(de352_run(&sc) == EIO && sc.step == 7 && !started);
	CHECK(sc.run_trace.phase == RT_OUTPUT && sc.run_trace.field_valid == 0);
	CHECK(sc.run_trace.result == EIO && sc.run_trace.valid == 3);
	CHECK(sc.run_trace.width == 720 && sc.run_trace.height == 480);
	CHECK(sc.run_trace.pixel_clock_hz == 27000000);
	for (unsigned i = 0; i < __arraycount(regs); i++) {
		init(&sc);
		run_readback_reg = regs[i];
		CHECK(de352_run(&sc) == EIO && sc.step == 7 && !started);
		CHECK(sc.run_trace.version == 1 && sc.run_trace.valid == 3);
		CHECK(sc.run_trace.phase == phases[i] && sc.run_trace.reg == regs[i]);
		CHECK(sc.run_trace.result == EIO && sc.run_trace.field_valid == 15);
		CHECK((sc.run_trace.after & sc.run_trace.mask) != sc.run_trace.requested);
		CHECK(sc.dma == NULL && sc.retained && releases == 0);
		saved = sc.run_trace;
		old_reads = reads;
		old_writes = writes;
		old_maps = space_maps;
		struct sysctlnode node = { &sc, sizeof(saved) };
		CHECK(de352_sysctl_run_trace(&node, NULL) == 0);
		CHECK(de352_sysctl_run_trace(&node, &sc) == EROFS);
		CHECK(de352_sysctl_output_trace(&node, NULL) == 0);
		CHECK(de352_sysctl_output_trace(&node, &sc) == EROFS);
		CHECK(de352_sysctl_phy_trace(&node, NULL) == 0);
		CHECK(de352_sysctl_phy_trace(&node, &sc) == EROFS);
		CHECK(memcmp(&saved, &sc.run_trace, sizeof(saved)) == 0);
		CHECK(reads == old_reads && writes == old_writes && space_maps == old_maps);
	}
	for (unsigned i = 0; i < 3; i++) {
		init(&sc);
		if (i < 2)
			run_read_fault = i + 1;
		else
			write_fault = 0x8050;
		CHECK(de352_run(&sc) == EIO && sc.step == 7);
		CHECK(sc.run_trace.phase == RT_CONFIGURE && sc.run_trace.reg == 0x8050);
		CHECK(sc.run_trace.field_valid == (i == 0 ? 5U : 7U));
		CHECK(sc.run_trace.after == 0 && sc.run_trace.result == EIO);
	}
	init(&sc);
	start_error = EIO;
	CHECK(de352_run(&sc) == EIO && sc.step == 8);
	CHECK(sc.run_trace.phase == RT_DONE && sc.run_trace.result == 0);
	CHECK(sc.run_trace.valid == 3);
	CHECK(sc.submit_trace.phase == ST_START && sc.submit_trace.result == EIO);
	CHECK(sc.submit_trace.valid == 3 && sc.submit_trace.request_samples == 0);
	CHECK(sc.submit_trace.regs_valid == 0x3ff && output_samples == 1);
}

static void
submit_trace_timeout(struct sun60i_de352_softc *sc, uint32_t phase)
{
	struct sysctlnode node = { .sysctl_data = sc };
	const struct de352_submit_trace *t = &sc->submit_trace;

	CHECK(t->version == 1 && t->valid == 3 && t->phase == phase);
	CHECK(t->result == ETIMEDOUT && t->request_samples > 0);
	CHECK(t->regs_valid == 0x3ff && t->regs[1] == de[0x8104 / 4]);
	CHECK(t->regs[7] == 32 && t->sample_error == 0 && output_samples == 1);
	CHECK(t->sample.version == 1 && t->sample.words[7] == 74);
	CHECK(de352_sysctl_submit_trace(&node, NULL) == 0);
	CHECK(de352_sysctl_submit_trace(&node, sc) == EROFS);
}

int main(void)
{
 run_trace_checks();
 guard_checks();
 cold_global_checks();
 cold_checks();
 cold_reset_checks();
 cold_trace_checks();
 struct sun60i_de352_softc sc;struct fdt_attach_args faa={1,NULL,(void *)(uintptr_t)2};
 init(&sc);CHECK(de352_match(NULL,NULL,&faa));de352_attach(NULL,&sc,&faa);CHECK(sc.valid&&reads==0&&writes==0&&space_maps==0);
 CHECK(de352_bindings(&sc));iommu_status="okay";CHECK(!de352_bindings(&sc));iommu_status=NULL;CHECK(!de352_bindings(&sc));iommu_status="disabled";
 nsi_status="okay";CHECK(!de352_bindings(&sc));nsi_status="disabled";
 for(int i=4;i<=5;i++){bad_base=i;CHECK(!de352_bindings(&sc));bad_base=0;wrong_compatible=i;CHECK(!de352_bindings(&sc));wrong_compatible=0;}
 for(int i=1;i<=3;i++){translated=i;CHECK(!de352_bindings(&sc));}translated=0;
 cell_override=3;CHECK(!de352_bindings(&sc));cell_override=0;property_length=8;CHECK(!de352_bindings(&sc));property_length=0;
 iommu_cells[1]=be32toh(7);CHECK(!de352_bindings(&sc));iommu_cells[1]=be32toh(8);nsi_cells[1]=be32toh(3);CHECK(!de352_bindings(&sc));
 init(&sc);power_cells[1]=be32toh(0);sc.valid=false;de352_attach(NULL,&sc,&faa);CHECK(!sc.valid&&reads==0&&writes==0);
 init(&sc);iommu_status="okay";sc.valid=false;de352_attach(NULL,&sc,&faa);CHECK(!sc.valid&&clock_puts==1&&reads==0&&writes==0);
 init(&sc);CHECK(de352_run(&sc)==0);CHECK(sc.step==10&&sc.retained&&sc.dma&&sc.dma->published&&sc.vblank==2&&releases==0&&faults==0);
 CHECK(sc.submit_trace.version==1&&sc.submit_trace.valid==3&&sc.submit_trace.phase==ST_DONE&&sc.submit_trace.result==0);
 CHECK(sc.submit_trace.request_busy>=1&&sc.submit_trace.poll_samples>=1&&sc.submit_trace.regs_valid==0&&output_samples==0);CHECK(de352_run(&sc)==EBUSY);reboot_fixture(&sc);
 for(unsigned i=0;i<7;i++){init(&sc);clocks[0][i]^=1;CHECK(de352_run(&sc)==EBUSY&&reads==0&&writes==0&&begins==0&&releases==1&&power_releases==1);}
 for(unsigned i=0;i<7;i++){init(&sc);clocks[0][i]=clocks[1][i]=0;CHECK(de352_run(&sc)==EOPNOTSUPP&&reads==0&&writes==0&&releases==1&&power_releases==1);}
 init(&sc);power_error=EBUSY;CHECK(de352_run(&sc)==EBUSY&&reads==0&&writes==0&&releases==0&&power_releases==0);
 init(&sc);reserve_error=EBUSY;CHECK(de352_run(&sc)==EBUSY&&reads==0&&writes==0&&releases==0&&power_releases==1);
 init(&sc);memory_error=EBUSY;CHECK(de352_run(&sc)==EBUSY&&reads==0&&begins==0&&releases==1&&power_releases==1);
 init(&sc);memory_error=EBUSY;ccu_release_error=EIO;
 CHECK(de352_run(&sc)==EBUSY&&sc.reserved&&sc.power_reserved&&claimed&&power_claimed&&power_releases==0&&sc.release_error==EIO);
 CHECK(de352_run(&sc)==EBUSY&&releases==1&&power_releases==0);
 init(&sc);memory_error=EBUSY;pck_release_error=EIO;
 CHECK(de352_run(&sc)==EBUSY&&!sc.reserved&&sc.power_reserved&&!claimed&&power_claimed&&power_releases==1&&sc.release_error==EIO);
 CHECK(de352_run(&sc)==EBUSY&&releases==1&&power_releases==1);
 const unsigned ir[]={0,0x14,0x838};for(unsigned i=0;i<3;i++){init(&sc);iommu[ir[i]/4]=i==2?1:(i==1?2:0);CHECK(de352_run(&sc)==EOPNOTSUPP&&writes==0&&begins==0&&releases==1&&power_releases==1);}
 init(&sc);iommu[0x804/4]=2;CHECK(de352_run(&sc)==EOPNOTSUPP&&writes==0&&releases==1&&power_releases==1);
 init(&sc);read_fault_region=2;CHECK(de352_run(&sc)==EIO&&writes==0&&releases==1&&power_releases==1);
 init(&sc);attempt_begin=false;begin_error=EBUSY;CHECK(de352_run(&sc)==EBUSY&&!sc.retained&&releases==1&&power_releases==1&&writes==0);
 init(&sc);begin_error=EIO;CHECK(de352_run(&sc)==EIO&&sc.retained&&releases==0&&faults==1&&ccu_faults==1&&writes==0);
 const unsigned idle_bad[]={0x8004,0x8100,0x8110,0x80d0,0x8020,0x281000,0x105000,0x105100,0x105400,0x125400,0x1c5400,0x1e5400,0x101000,0x121030,0x141090,0x1c1060,0x1e1000,0x201040,0x221060,0x285000,0x289040,0x28b000};
 for(unsigned i=0;i<__arraycount(idle_bad);i++){init(&sc);de[idle_bad[i]/4]=1;CHECK(de352_run(&sc)==EBUSY&&sc.retained&&sc.dma==NULL&&releases==0);}
 init(&sc);de[0x8000/4]=1;CHECK(de352_run(&sc)==EBUSY&&sc.retained&&writes==0&&sc.dma==NULL);
 init(&sc);de[0x8028/4]=1;CHECK(de352_run(&sc)==EOPNOTSUPP&&writes==0&&sc.retained);
 init(&sc);de[0x8010/4]=0x40;CHECK(de352_run(&sc)==EBUSY&&writes==0&&sc.retained);
 /* Only TV0 mux value 4 is a conflict. Unused value 3 in another field is not. */
 init(&sc);de[0x8010/4]=0xfff0;CHECK(de352_run(&sc)==0&&de[0x8010/4]==0xfff4);reboot_fixture(&sc);
 init(&sc);de[0x8010/4]=0x30;CHECK(de352_run(&sc)==0&&(de[0x8010/4]&0xf)==4);reboot_fixture(&sc);
 init(&sc);write_fault=0x8004;CHECK(de352_run(&sc)==EIO&&sc.retained&&sc.dma==NULL&&releases==0);
 init(&sc);mode_error=ENXIO;CHECK(de352_run(&sc)==ENXIO&&sc.retained&&sc.dma==NULL);
 init(&sc);prepare_error=EIO;CHECK(de352_run(&sc)==EIO&&sc.retained&&sc.dma==NULL);reset();
 init(&sc);write_fault=0x8114;CHECK(de352_run(&sc)==EIO&&sc.retained&&sc.dma&&sc.dma->published&&releases==0);reboot_fixture(&sc);
 init(&sc);de[0x8104/4]=0xc;no_finish=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);bad_w1c=true;de[0x8104/4]=0xc;CHECK(de352_run(&sc)==EIO&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);no_busy=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);
 submit_trace_timeout(&sc,ST_REQUEST);CHECK(sc.submit_trace.request_busy==0&&sc.submit_trace.poll_samples==0);
 CHECK(sc.submit_trace.request_control==0&&(sc.submit_trace.status_or&0x10)==0);reboot_fixture(&sc);
 init(&sc);no_finish=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);
 submit_trace_timeout(&sc,ST_POLL);CHECK(sc.submit_trace.request_busy==1&&sc.submit_trace.poll_samples>1);
 CHECK((sc.submit_trace.poll_last&4)==0&&(sc.submit_trace.status_and&0x10)==0x10);reboot_fixture(&sc);
 init(&sc);no_busy=true;output_sample_error=EFAULT;CHECK(de352_run(&sc)==ETIMEDOUT);
 CHECK(sc.submit_trace.sample_error==EFAULT&&sc.submit_trace.valid==3&&output_samples==1);reboot_fixture(&sc);
 init(&sc);no_copy=true;CHECK(de352_run(&sc)==EIO&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);no_vblank=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);same_sequence=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);memory_fault_call=3;CHECK(de352_run(&sc)==EIO&&sc.dma&&sc.dma->published&&sc.memory.valid==0&&ccu_faults==1);reboot_fixture(&sc);
 init(&sc);clocks[1][0]=0;CHECK(de352_run(&sc)==EBUSY&&sc.memory.version==1&&sc.memory.valid==1&&sc.memory.clocks[1][0]==0);
 struct sysctlnode memnode={&sc,sizeof(sc.memory)};CHECK(de352_sysctl_memory(&memnode,NULL)==0);CHECK(de352_sysctl_memory(&memnode,&sc)==EROFS);
 init(&sc);read_fault_region=3;read_fault_reg=0x414;CHECK(de352_run(&sc)==EIO&&sc.memory.valid==1&&sc.memory.iommu[0][0]==0&&sc.memory.nsi[0][0]==0);
 init(&sc);struct sysctlnode n={&sc.stage,sizeof(sc.stage)};u_int stage=2;CHECK(de352_sysctl(&n,&stage)==EINVAL&&writes==0);stage=1;CHECK(de352_sysctl(&n,&stage)==0&&sc.result==0);CHECK(de352_sysctl(&n,&stage)==EBUSY&&sc.result==0);reboot_fixture(&sc);
 managed_checks();
 scanout_checks();
 printf("A733 DE3.52 native coordinator: %u checks passed\n",checks);return 0;
}
