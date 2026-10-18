/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; actual native coordinator fault controls. */
#define main dma_fixture_unused_main
#include "a733-de352-dma-contract.c"
#undef main
#include <stdarg.h>
#include <stddef.h>
#include <time.h>

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
static uint32_t read_fault_reg;
static uint64_t sequence;
static unsigned event_polls;
static uint32_t de[0x400000/4], iommu[0x840/4], nsi[0x420/4], clocks[2][7];
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
{ (void)t;(void)flags;CHECK((a==0x5000000&&n==0x400000)||(a==0x3910000&&n==0x840)||(a==0x2020000&&n==0x420));*h=a==0x5000000?1:a==0x3910000?2:3;space_maps++;return 0; }
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
static int sun60i_a733_ccu_display_release(struct clk *c,const void *owner)
{ CHECK(c==&clock_de0&&claimed&&!retained&&owner==lease_owner);releases++;if(ccu_release_error)return ccu_release_error;claimed=memory_claimed=false;return 0; }
static int sun60i_a733_ccu_display_memory_state(struct clk *c,const void *owner,uint32_t out[2][7])
{ CHECK(c==&clock_de0&&claimed&&memory_claimed&&owner==lease_owner);memory_calls++;if(memory_calls==memory_fault_call)return EIO;memcpy(out,clocks,sizeof(clocks));return 0; }
static int sun60i_a733_ccu_display_begin_de0(struct clk *c,const void *owner,bool *attempted)
{ CHECK(c==&clock_de0&&claimed&&memory_claimed&&owner==lease_owner);begins++;*attempted=attempt_begin;retained=attempt_begin;return begin_error; }
static int sun60i_a733_ccu_display_fault(struct clk *c,const void *owner,int error)
{ CHECK(c==&clock_de0&&owner==lease_owner&&claimed&&retained&&error);ccu_faults++;return error; }
#include "sun60i_a733_output.h"
static struct sun60i_a733_output_mode mode={27000000,720,736,798,858,480,489,495,525,false,false};
int sun60i_a733_output_get_mode(int node,const void *owner,struct sun60i_a733_output_mode *out)
{ CHECK(node==7&&owner==lease_owner&&retained);if(mode_error)return mode_error;*out=mode;return 0; }
int sun60i_a733_output_prepare(int node,const void *owner,const struct sun60i_a733_output_mode *m)
{ CHECK(node==7&&owner==lease_owner&&retained&&m->hdisplay==720);return prepare_error; }
int sun60i_a733_output_start(int node,const void *owner)
{ CHECK(node==7&&owner==lease_owner&&retained&&(de[0x8100/4]&1));if(start_error)return start_error;started=true;if(!no_busy)de[0x8104/4]|=0x10;return 0; }
int sun60i_a733_output_observed_vblank(int node,const void *owner,uint64_t *out)
{ CHECK(node==7&&owner==lease_owner&&retained&&started);event_polls++;if(no_vblank||event_polls%2)return ENOENT;if(!same_sequence)sequence++;*out=sequence;return 0; }
int sun60i_a733_output_fault(int node,const void *owner,int error)
{ CHECK(node==7&&owner==lease_owner&&retained&&error);faults++;return error; }
#include "sun60i_a733_de352_fdt.c"

static int bus_space_peek_4(bus_space_tag_t tag,bus_space_handle_t h,bus_size_t off,uint32_t *out)
{
 (void)tag;reads++;CHECK((off&3)==0);
 if((int)h==read_fault_region&&off==read_fault_reg)return EIO;
 if(h==1){CHECK(retained&&off<sizeof(de));*out=de[off/4];}
 else if(h==2){CHECK(memory_claimed&&off<sizeof(iommu));*out=iommu[off/4];}
 else {CHECK(h==3&&memory_claimed&&off<sizeof(nsi));*out=nsi[off/4];}
 return 0;
}
static int bus_space_poke_4(bus_space_tag_t tag,bus_space_handle_t h,bus_size_t off,uint32_t value)
{
 struct sun60i_de352_softc *sc=active_softc;
 (void)tag;CHECK(h==1&&retained&&off<sizeof(de));writes++;
 if(off>=0x8114&&off<=0x811c)CHECK(sc->dma&&sc->dma->published&&sc->dma->synchronized&&syncs==2);
 if((int)off==write_fault)return EIO;
 if(off==0x8104){if(!bad_w1c)de[off/4]&=~(value&0xd);return 0;}
 if(off==0x8110){
  CHECK(irq_masked&&started&&sc->dma->published&&(de[0x8104/4]&0x10)&&value==1);
  CHECK(de[0x8114/4]==sc->dma->queue.address&&de[0x8118/4]==0&&de[0x811c/4]==32);
  de[off/4]=0;
  if(no_finish)return 0;
  if(!no_copy)for(unsigned i=0;i<2;i++){
   uint8_t *d=(uint8_t *)sc->dma->queue.cpu+i*16;
   uint32_t src=get32(d),n=get32(d+4)&0xffffff,dst=get32(d+12);
   CHECK(src>=sc->dma->queue.address&&src+n<=sc->dma->queue.address+sc->dma->queue.bytes&&dst+n<=sizeof(de));
   for(unsigned j=0;j<n;j+=4)de[(dst+j)/4]=get32((uint8_t *)sc->dma->queue.cpu+(src-sc->dma->queue.address)+j);
  }
  de[0x8104/4]|=4;return 0;
 }
 de[off/4]=value;return 0;
}
static void init(struct sun60i_de352_softc *sc)
{
 reset();memset(sc,0,sizeof(*sc));memset(de,0,sizeof(de));memset(iommu,0,sizeof(iommu));memset(nsi,0,sizeof(nsi));
 tick=0;reads=writes=space_maps=space_unmaps=releases=faults=begins=0;
 power_error=power_releases=ccu_release_error=pck_release_error=ccu_faults=0;
 reserve_error=memory_error=begin_error=mode_error=prepare_error=start_error=0;
 attempt_begin=true;claimed=memory_claimed=retained=started=irq_masked=power_claimed=false;lease_owner=NULL;
 no_busy=no_finish=no_copy=no_vblank=same_sequence=bad_w1c=false;
 read_fault_region=write_fault=memory_calls=memory_fault_call=0;read_fault_reg=0;sequence=0;event_polls=0;
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
 /* The fixture models a reset after asserting retention. Production cannot do this. */
 if(sc->dma){sc->dma->published=false;CHECK(sun60i_a733_de352_dma_destroy(sc->dma)==0);sc->dma=NULL;}
 reset();
}
int main(void)
{
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
 init(&sc);CHECK(de352_run(&sc)==0);CHECK(sc.step==10&&sc.retained&&sc.dma&&sc.dma->published&&sc.vblank==2&&releases==0&&faults==0);CHECK(de352_run(&sc)==EBUSY);reboot_fixture(&sc);
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
 const unsigned ir[]={0,0x10,0x14,0x804,0x838};for(unsigned i=0;i<5;i++){init(&sc);iommu[ir[i]/4]=i==4?1:0;CHECK(de352_run(&sc)==EOPNOTSUPP&&writes==0&&begins==0&&releases==1&&power_releases==1);}
 init(&sc);nsi[0x418/4]=0;CHECK(de352_run(&sc)==EOPNOTSUPP&&writes==0&&releases==1&&power_releases==1);
 init(&sc);read_fault_region=2;CHECK(de352_run(&sc)==EIO&&writes==0&&releases==1&&power_releases==1);
 init(&sc);attempt_begin=false;begin_error=EBUSY;CHECK(de352_run(&sc)==EBUSY&&!sc.retained&&releases==1&&power_releases==1&&writes==0);
 init(&sc);begin_error=EIO;CHECK(de352_run(&sc)==EIO&&sc.retained&&releases==0&&faults==1&&ccu_faults==1&&writes==0);
 const unsigned idle_bad[]={0x8004,0x8100,0x8110,0x80d0,0x8020,0x281000,0x105000,0x105100,0x105400,0x125400,0x1c5400,0x1e5400,0x101000,0x121030,0x141090,0x1c1060,0x1e1000,0x201040,0x221060,0x285000,0x289040,0x28b000};
 for(unsigned i=0;i<__arraycount(idle_bad);i++){init(&sc);de[idle_bad[i]/4]=1;CHECK(de352_run(&sc)==EBUSY&&sc.retained&&sc.dma==NULL&&releases==0);}
 init(&sc);de[0x8028/4]=1;CHECK(de352_run(&sc)==EOPNOTSUPP&&writes==0&&sc.retained);
 init(&sc);de[0x8010/4]=0x30;CHECK(de352_run(&sc)==EBUSY&&writes==0&&sc.retained);
 init(&sc);write_fault=0x8004;CHECK(de352_run(&sc)==EIO&&sc.retained&&sc.dma==NULL&&releases==0);
 init(&sc);mode_error=ENXIO;CHECK(de352_run(&sc)==ENXIO&&sc.retained&&sc.dma==NULL);
 init(&sc);prepare_error=EIO;CHECK(de352_run(&sc)==EIO&&sc.retained&&sc.dma==NULL);reset();
 init(&sc);write_fault=0x8114;CHECK(de352_run(&sc)==EIO&&sc.retained&&sc.dma&&sc.dma->published&&releases==0);reboot_fixture(&sc);
 init(&sc);de[0x8104/4]=0xc;no_finish=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);bad_w1c=true;de[0x8104/4]=0xc;CHECK(de352_run(&sc)==EIO&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);no_busy=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);no_finish=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);no_copy=true;CHECK(de352_run(&sc)==EIO&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);no_vblank=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);same_sequence=true;CHECK(de352_run(&sc)==ETIMEDOUT&&sc.dma&&sc.dma->published);reboot_fixture(&sc);
 init(&sc);memory_fault_call=3;CHECK(de352_run(&sc)==EIO&&sc.dma&&sc.dma->published&&sc.memory.valid==0&&ccu_faults==1);reboot_fixture(&sc);
 init(&sc);clocks[1][0]=0;CHECK(de352_run(&sc)==EBUSY&&sc.memory.version==1&&sc.memory.valid==1&&sc.memory.clocks[1][0]==0);
 struct sysctlnode memnode={&sc,sizeof(sc.memory)};CHECK(de352_sysctl_memory(&memnode,NULL)==0);CHECK(de352_sysctl_memory(&memnode,&sc)==EROFS);
 init(&sc);read_fault_region=3;read_fault_reg=0x414;CHECK(de352_run(&sc)==EIO&&sc.memory.valid==1&&sc.memory.iommu[0][0]==0&&sc.memory.nsi[0][0]==0);
 init(&sc);struct sysctlnode n={&sc.stage,sizeof(sc.stage)};u_int stage=2;CHECK(de352_sysctl(&n,&stage)==EINVAL&&writes==0);stage=1;CHECK(de352_sysctl(&n,&stage)==0&&sc.result==0);CHECK(de352_sysctl(&n,&stage)==EBUSY&&sc.result==0);reboot_fixture(&sc);
 printf("A733 DE3.52 native coordinator: %u checks passed\n",checks);return 0;
}
