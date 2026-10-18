/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual control/boot source; platform services only are modeled. */
#define cixvpu_acpi_boot model_cixvpu_acpi_boot
#define cixvpu_acpi_boot_create model_cixvpu_acpi_boot_create
#define cixvpu_acpi_boot_create_rpc model_cixvpu_acpi_boot_create_rpc
#define cixvpu_acpi_boot_create_frame model_cixvpu_acpi_boot_create_frame
#define cixvpu_acpi_boot_once model_cixvpu_acpi_boot_once
#define cixvpu_acpi_boot_report model_cixvpu_acpi_boot_report
#define device_xname control_device_xname
#define main cixvpu_control_contract_main
#define cixvpu_dma model_dma
#define cixvpu_dma_page model_dma_page
#define cixvpu_dma_alloc model_dma_alloc
#define cixvpu_dma_free model_dma_free
#define cixvpu_prepared model_prepared
#define cixvpu_prepare_cpu model_prepare_cpu
#define cixvpu_prepare_boot_cpu model_prepare_boot_cpu
#define cixvpu_frame_source model_frame_source
#define cixvpu_prepare_frame_slots_cpu model_prepare_frame_slots_cpu
#define cixvpu_prepared_free model_prepared_free
#define cixvpu_prepared_sync_initial model_prepared_sync_initial
#define cixvpu_prepared_publish_root model_prepared_publish_root
#define cixvpu_prepared_boot_receive model_prepared_boot_receive
#define cixvpu_boot_reply model_boot_reply
#define cixvpu_boot_rpc_header model_boot_rpc_header
#define cixvpu_prepared_boot_rpc_header model_prepared_boot_rpc_header
#define cixvpu_prepared_boot_debugparam model_prepared_boot_debugparam
#define cixvpu_boot_debug_result model_boot_debug_result
#define CIXVPU_BOOT_DEBUG_IDLE MODEL_BOOT_DEBUG_IDLE
#define CIXVPU_BOOT_DEBUG_PENDING MODEL_BOOT_DEBUG_PENDING
#define CIXVPU_BOOT_DEBUG_REPLIED MODEL_BOOT_DEBUG_REPLIED
#define cixvpu_boot_rpc_result model_boot_rpc_result
#define cixvpu_prepared_boot_rpc_step model_prepared_boot_rpc_step
#define cixvpu_prepared_boot_receive_rpc model_prepared_boot_receive_rpc
#define CIXVPU_BOOT_RPC_IDLE MODEL_BOOT_RPC_IDLE
#define CIXVPU_BOOT_RPC_PENDING MODEL_BOOT_RPC_PENDING
#define CIXVPU_BOOT_RPC_ALLOC_REPLIED MODEL_BOOT_RPC_ALLOC_REPLIED
#define CIXVPU_BOOT_RPC_DEBUG_REPLIED MODEL_BOOT_RPC_DEBUG_REPLIED
#define CIXVPU_BOOT_RPC_RESIZE_REPLIED MODEL_BOOT_RPC_RESIZE_REPLIED
#define CIXVPU_BOOT_RPC_SECOND_ALLOC_REPLIED MODEL_BOOT_RPC_SECOND_ALLOC_REPLIED
#define cixvpu_queue_identity MODEL_cixvpu_queue_identity
#define cixvpu_queue_command_kind MODEL_cixvpu_queue_command_kind
#define CIXVPU_Q_JOB_ONE MODEL_CIXVPU_Q_JOB_ONE
#define CIXVPU_Q_NALU_START_CODES MODEL_CIXVPU_Q_NALU_START_CODES
#define CIXVPU_Q_BFRAMES_ZERO MODEL_CIXVPU_Q_BFRAMES_ZERO
#define CIXVPU_Q_PFRAMES_ZERO MODEL_CIXVPU_Q_PFRAMES_ZERO
#define CIXVPU_Q_HEVC_MAIN MODEL_CIXVPU_Q_HEVC_MAIN
#define CIXVPU_Q_FIXED_QP MODEL_CIXVPU_Q_FIXED_QP
#define CIXVPU_Q_GO MODEL_CIXVPU_Q_GO
#define CIXVPU_Q_STOP MODEL_CIXVPU_Q_STOP
#define CIXVPU_Q_INPUT_FLUSH MODEL_CIXVPU_Q_INPUT_FLUSH
#define CIXVPU_Q_OUTPUT_FLUSH MODEL_CIXVPU_Q_OUTPUT_FLUSH
#define CIXVPU_Q_PING MODEL_CIXVPU_Q_PING
#define CIXVPU_Q_IDLE_ACK MODEL_CIXVPU_Q_IDLE_ACK
#define cixvpu_queue_command MODEL_cixvpu_queue_command
#define cixvpu_queue_param_kind MODEL_cixvpu_queue_param_kind
#define CIXVPU_Q_FRAME_RATE MODEL_CIXVPU_Q_FRAME_RATE
#define CIXVPU_Q_RC_OFF MODEL_CIXVPU_Q_RC_OFF
#define cixvpu_queue_input_param MODEL_cixvpu_queue_input_param
#define cixvpu_queue_slot MODEL_cixvpu_queue_slot
#define CIXVPU_Q_INPUT_ONCE MODEL_CIXVPU_Q_INPUT_ONCE
#define CIXVPU_Q_OUTPUT_EMPTY MODEL_CIXVPU_Q_OUTPUT_EMPTY
#define cixvpu_queue_event_kind MODEL_cixvpu_queue_event_kind
#define CIXVPU_Q_RESPONSE MODEL_CIXVPU_Q_RESPONSE
#define CIXVPU_Q_INPUT_AVAILABLE MODEL_CIXVPU_Q_INPUT_AVAILABLE
#define CIXVPU_Q_OUTPUT_AVAILABLE MODEL_CIXVPU_Q_OUTPUT_AVAILABLE
#define CIXVPU_Q_PONG MODEL_CIXVPU_Q_PONG
#define CIXVPU_Q_IDLE MODEL_CIXVPU_Q_IDLE
#define CIXVPU_Q_INPUT_FLUSHED MODEL_CIXVPU_Q_INPUT_FLUSHED
#define CIXVPU_Q_OUTPUT_FLUSHED MODEL_CIXVPU_Q_OUTPUT_FLUSHED
#define cixvpu_queue_event MODEL_cixvpu_queue_event
#define cixvpu_prepared_queue_open MODEL_cixvpu_prepared_queue_open
#define cixvpu_prepared_queue_rpc_pending MODEL_cixvpu_prepared_queue_rpc_pending
#define cixvpu_prepared_queue_command MODEL_cixvpu_prepared_queue_command
#define cixvpu_prepared_queue_input_param MODEL_cixvpu_prepared_queue_input_param
#define cixvpu_prepared_queue_submit MODEL_cixvpu_prepared_queue_submit
#define cixvpu_prepared_queue_receive MODEL_cixvpu_prepared_queue_receive
#define cixvpu_prepared_queue_input_return MODEL_cixvpu_prepared_queue_input_return
#define cixvpu_prepared_queue_output_copy MODEL_cixvpu_prepared_queue_output_copy
#define cixvpu_prepared_queue_hold MODEL_cixvpu_prepared_queue_hold
#define delay control_delay
#define kmem_free control_kmem_free
#define bus_space_read_4 control_read_4
#define bus_space_barrier control_barrier
#include "cixvpu_command.h"
#include "cixvpu_buffer.h"
#include "cixvpu-control-contract.c"
#undef kmem_free
#undef main
#undef device_xname
#undef cixvpu_acpi_boot
#undef cixvpu_acpi_boot_create
#undef cixvpu_acpi_boot_create_rpc
#undef cixvpu_acpi_boot_create_frame
#undef cixvpu_acpi_boot_once
#undef cixvpu_acpi_boot_report
#undef cixvpu_dma
#undef cixvpu_dma_page
#undef cixvpu_dma_alloc
#undef cixvpu_dma_free
#undef cixvpu_prepared
#undef cixvpu_prepare_cpu
#undef cixvpu_prepare_boot_cpu
#undef cixvpu_frame_source
#undef cixvpu_prepare_frame_slots_cpu
#undef cixvpu_prepared_free
#undef cixvpu_prepared_sync_initial
#undef cixvpu_prepared_publish_root
#undef cixvpu_prepared_boot_receive
#undef cixvpu_boot_reply
#undef cixvpu_boot_rpc_header
#undef cixvpu_prepared_boot_rpc_header
#undef cixvpu_prepared_boot_debugparam
#undef cixvpu_boot_debug_result
#undef CIXVPU_BOOT_DEBUG_IDLE
#undef CIXVPU_BOOT_DEBUG_PENDING
#undef CIXVPU_BOOT_DEBUG_REPLIED
#undef cixvpu_boot_rpc_result
#undef cixvpu_prepared_boot_rpc_step
#undef cixvpu_prepared_boot_receive_rpc
#undef CIXVPU_BOOT_RPC_IDLE
#undef CIXVPU_BOOT_RPC_PENDING
#undef CIXVPU_BOOT_RPC_ALLOC_REPLIED
#undef CIXVPU_BOOT_RPC_DEBUG_REPLIED
#undef CIXVPU_BOOT_RPC_RESIZE_REPLIED
#undef CIXVPU_BOOT_RPC_SECOND_ALLOC_REPLIED
#undef bus_space_read_4
#undef bus_space_barrier
#undef delay
#undef cixvpu_queue_identity
#undef cixvpu_queue_command_kind
#undef CIXVPU_Q_JOB_ONE
#undef CIXVPU_Q_NALU_START_CODES
#undef CIXVPU_Q_BFRAMES_ZERO
#undef CIXVPU_Q_PFRAMES_ZERO
#undef CIXVPU_Q_HEVC_MAIN
#undef CIXVPU_Q_FIXED_QP
#undef CIXVPU_Q_GO
#undef CIXVPU_Q_STOP
#undef CIXVPU_Q_INPUT_FLUSH
#undef CIXVPU_Q_OUTPUT_FLUSH
#undef CIXVPU_Q_PING
#undef CIXVPU_Q_IDLE_ACK
#undef cixvpu_queue_command
#undef cixvpu_queue_param_kind
#undef CIXVPU_Q_FRAME_RATE
#undef CIXVPU_Q_RC_OFF
#undef cixvpu_queue_input_param
#undef cixvpu_queue_slot
#undef CIXVPU_Q_INPUT_ONCE
#undef CIXVPU_Q_OUTPUT_EMPTY
#undef cixvpu_queue_event_kind
#undef CIXVPU_Q_RESPONSE
#undef CIXVPU_Q_INPUT_AVAILABLE
#undef CIXVPU_Q_OUTPUT_AVAILABLE
#undef CIXVPU_Q_PONG
#undef CIXVPU_Q_IDLE
#undef CIXVPU_Q_INPUT_FLUSHED
#undef CIXVPU_Q_OUTPUT_FLUSHED
#undef cixvpu_queue_event
#undef cixvpu_prepared_queue_open
#undef cixvpu_prepared_queue_rpc_pending
#undef cixvpu_prepared_queue_command
#undef cixvpu_prepared_queue_input_param
#undef cixvpu_prepared_queue_submit
#undef cixvpu_prepared_queue_receive
#undef cixvpu_prepared_queue_input_return
#undef cixvpu_prepared_queue_output_copy
#undef cixvpu_prepared_queue_hold
#ifdef CIXVPU_BOOT_ORCH_API
#include <sys/time.h>
#undef _DEV_IC_CIXVPU_PREPARE_H_
struct cixvpu_dma;
#include "real-prepare-header.inc"
#include "boot-header.inc"
extern struct cixvpu_prepared *boot_fixture_prepare(void);
extern void boot_fixture_runtime(bool);
extern bool boot_fixture_exposed(struct cixvpu_prepared *);
extern void boot_fixture_drop(struct cixvpu_prepared *);
extern struct cixvpu_prepared *boot_fixture_prepare_kind(bool);
extern void boot_fixture_set_ack(void (*)(void));
extern unsigned int boot_fixture_acks(void);
extern unsigned int boot_fixture_live(void);
extern void boot_fixture_rpc(uint32_t, uint32_t, uint32_t);
extern void boot_fixture_rpc_alloc(void);
extern void boot_fixture_rpc_resize(uint32_t, uint32_t);
extern void boot_fixture_map_valid(struct cixvpu_prepared *, bool);
extern struct cixvpu_prepared *boot_fixture_prepare_rpc(void);
extern void boot_fixture_rpc_words(uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, uint32_t, uint32_t);
extern uint32_t boot_fixture_rpc_word(unsigned int);
extern void boot_fixture_set_rpc_commit(void (*)(void));
extern struct cixvpu_prepared *boot_fixture_prepare_frame(void);
extern void boot_fixture_frame_start(unsigned int);
extern void boot_fixture_frame_notify(void);
extern unsigned int boot_fixture_frame_publications(void);
extern void boot_fixture_frame_tick(void);

/* The PIC model starts masked and can dispatch synchronously on unmask. */
#define KM_NOSLEEP 0x00000002
#define IPL_VM 6
#define IST_LEVEL 3
#define IST_MPSAFE 0x100
#define BUS_SPACE_BARRIER_WRITE 2
static unsigned int boot_masks, boot_unmasks, boot_intrs, boot_disestablish;
static unsigned int boot_irq_count, boot_writes, boot_delays;
static unsigned int boot_fault;
static bool boot_rpc_test;
static unsigned int boot_rpc_commit_events, boot_rpc_notify_events;
static unsigned int boot_rpc_deferrals;
static bool frame_bootstrap_test;
static unsigned int frame_bootstrap_notifies;
static bool frame_trial;
static unsigned int frame_scenario,frame_command_calls,frame_gate_calls,frame_fault_acks;
static bool frame_rpc_faulted;
static bool boot_term_issued;
static uint64_t boot_now;
static uint32_t boot_regs[2][0x400 / 4];
static struct { bus_space_handle_t h; bus_size_t offset; uint32_t value; } boot_trace[128];
static unsigned int boot_barriers;
static int (*boot_handler)(void *);
static void *boot_handler_arg;
static unsigned int boot_pic_mask_count;
static struct cixvpu_prepared *boot_actual;
static void *kmem_zalloc(size_t n, int f)
{ (void)f; if (boot_fault == 1 || (boot_fault==63 && n==262144)) return NULL; return allocate(n); }
static void kmem_free(void *p,size_t n) {CHECK(n!=0);AcpiOsFree(p);}
static unsigned int atomic_cas_uint(volatile unsigned int *p, unsigned int a, unsigned int b)
{ unsigned int old = *p; if (old == a) *p = b; return old; }
static void atomic_or_uint(volatile unsigned int *p, unsigned int v) { *p |= v; }
#define atomic_load_acquire(p) (*(p))
#define atomic_store_release(p,v) (*(p)=(v))
static void *intr_establish_xname_masked(int irq, int ipl, int type,
    int (*fn)(void *), void *arg, const char *name)
{
    CHECK(irq == 358 && ipl == IPL_VM && type == (IST_LEVEL | IST_MPSAFE));
    CHECK(name != NULL && fn != NULL && arg != NULL); boot_intrs++;
    CHECK(maps==0 && !power_live && !clock_live);
    assert(fn != NULL && arg != NULL);
    if (boot_fault==22) CHECK(fn(arg)==0 && boot_writes==0);
    if (boot_fault == 2) return NULL;
    CHECK(boot_handler == NULL); boot_handler = fn; boot_handler_arg = arg;
    boot_pic_mask_count = 1; return &boot_handler;
}
static void intr_mask(void *cookie)
{ CHECK(cookie == &boot_handler && boot_pic_mask_count == 0); boot_pic_mask_count++; boot_masks++; }
static void intr_unmask(void *cookie)
{
    CHECK(cookie == &boot_handler && boot_pic_mask_count == 1);
    boot_pic_mask_count--; boot_unmasks++;
    if (boot_fault == 6) faults |= F_RESET_ASSERT;
    if (boot_fault>=13 && boot_fault<=20) {
        static const unsigned int cleanup[]={F_RESET_ASSERT,F_CLOCK_OFF,F_POWER_OFF,
            F_STA_OFF,F_STA_OFF_FAIL,F_PUT_MAIN,F_PUT_RCSU,F_PUT_CLOCK};
        faults |= cleanup[boot_fault-13];
    }
    if (boot_fault == 3) { boot_regs[1][0x18 / 4] = 2; boot_handler(boot_handler_arg); }
}
static void intr_disestablish(void *cookie)
{ CHECK(cookie == &boot_handler && boot_pic_mask_count == 1); boot_disestablish++; boot_handler = NULL; }
static uint32_t bus_space_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t offset)
{
    (void)t; CHECK((h == 1 || h == 2) && offset < 0x400 && offset % 4 == 0);
    if (h == 1 && offset == 0x218) {
        boot_irq_count++;
        if (!boot_term_issued || boot_fault==4 || boot_fault==11 || boot_fault==12)
            return boot_regs[1][offset / 4];
        return 0;
    }
    return boot_regs[h == 1 ? 1 : 0][offset / 4];
}
static void boot_final_ack_irq(void);
static void boot_rpc_request(void);
static void boot_rpc_commit_hook(void);
static void boot_rpc_notified(void);
static void frame_bootstrap_notified(void);
static void bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, uint32_t value)
{
    (void)t; CHECK((h == 1 || h == 2) && offset < 0x400 && offset % 4 == 0);
    CHECK(boot_writes==boot_barriers);
    assert(boot_writes < sizeof(boot_trace)/sizeof(boot_trace[0]));
    boot_trace[boot_writes].h=h; boot_trace[boot_writes].offset=offset;
    boot_trace[boot_writes++].value=value;
    if (h==1 && offset==0x204 && boot_fault==10) boot_regs[1][0x14/4]=0;
    if (h == 1 && offset == 0x204) CHECK(boot_fixture_exposed(boot_actual));
    boot_regs[h == 1 ? 1 : 0][offset / 4] = value;
    if (h==1 && offset==0x218 && value==1) boot_term_issued=true;
    if (h==1 && offset==4 && value==1) {
        boot_fixture_runtime(!boot_rpc_test && boot_fault != 7 && boot_fault != 5 && boot_fault != 21);
        if(frame_trial)boot_fixture_frame_start(frame_scenario);
        if (boot_rpc_test) {
            boot_fixture_set_rpc_commit(boot_rpc_commit_hook);
            boot_rpc_request();
        }
        if (boot_fault == 9) boot_fixture_set_ack(boot_final_ack_irq);
        if (boot_fault == 23 || boot_fault == 24) boot_fixture_rpc(1,0x12345678,0xfedcba98);
        if (boot_fault == 25) boot_fixture_rpc_alloc();
    }
    if(h==1 && offset==0x220 && value==1 && boot_regs[1][1]==1 && frame_trial)
        boot_fixture_frame_notify();
    else if(h==1 && offset==0x220 && value==1 && frame_bootstrap_test)
        frame_bootstrap_notified();
    else if(h==1 && offset==0x220 && value==1 && boot_regs[1][1]==1 && boot_rpc_test)
        boot_rpc_notified();
    if (h == 1 && offset == 0x21c && value == 0) boot_regs[1][0x18 / 4] &= ~1U;
}
static void bus_space_barrier(bus_space_tag_t t, bus_space_handle_t h,
    bus_size_t offset, bus_size_t length, int flags)
{ (void)t; CHECK(h == 1 || h == 2); CHECK(offset <= 0x10000 && length <= 0x10000-offset); CHECK(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)); CHECK(boot_writes==boot_barriers+1); CHECK(boot_trace[boot_barriers].h==h && boot_trace[boot_barriers].offset==offset && length==4); boot_barriers++;
  if(h==1 && offset==0x220 && boot_regs[1][1]==1 && boot_rpc_test && (boot_fault==33 || (boot_fault==43 && boot_rpc_notify_events==2) || (boot_fault==51 && boot_rpc_notify_events==3))) {
      boot_regs[1][0x18/4]=2; CHECK(boot_handler(boot_handler_arg)==1);
  }
}
static void getmicrouptime(struct timeval *t)
{ if(frame_trial && frame_scenario==30 && boot_fixture_frame_publications()>=2) {} else if (boot_fault==11 || boot_fault==21) boot_now=boot_now==0 ? 100000 : boot_now-100; else if (boot_fault!=5 && boot_fault!=12) boot_now+=10; t->tv_sec=boot_now/1000000; t->tv_usec=boot_now%1000000; }
static void delay(unsigned int usec)
{ if(frame_trial)boot_fixture_frame_tick();boot_delays++; if(frame_trial && frame_scenario==30 && boot_fixture_frame_publications()>=2)return; if (boot_fault!=5 && boot_fault!=12 && boot_fault!=11) boot_now+=usec; }
static const char *device_xname(device_t d) { CHECK(d==VPU); return "cixvpu0"; }
static void boot_final_ack_irq(void)
{
    if (boot_fixture_acks() == 2) {
        boot_regs[1][0x18 / 4] = 3;
        CHECK(boot_handler(boot_handler_arg) == 1);
    }
}
static int boot_publish_actual(struct cixvpu_prepared *p, uint32_t *root)
{
    int error = cixvpu_prepared_publish_root(p, root);
    if (error == 0 && boot_fault == 8) {
        boot_regs[1][0x18 / 4] = 2;
        CHECK(boot_handler(boot_handler_arg) == 1);
    }
    return error;
}
static int boot_rpc_header_actual(struct cixvpu_prepared *p, struct cixvpu_boot_rpc_header *out)
{ if(boot_fault==24) return EIO; return cixvpu_prepared_boot_rpc_header(p,out); }
static int boot_rpc_step_actual(struct cixvpu_prepared *p, enum cixvpu_boot_rpc_result *out)
{
    /* Inject one legal zero-I/O EAGAIN at the native coordinator boundary. */
    if(boot_fault==36 && boot_rpc_deferrals++==0) return EAGAIN;
    int error=cixvpu_prepared_boot_rpc_step(p,out);
    if(frame_trial && !frame_rpc_faulted && error==0 && *out==CIXVPU_BOOT_RPC_IDLE &&
        ((frame_scenario==15 || frame_scenario==16) ? boot_fixture_frame_publications()==8 :
        frame_scenario==17 && boot_fixture_frame_publications()==11 && boot_fixture_acks()==9)) {
        frame_rpc_faulted=true;frame_fault_acks=boot_fixture_acks();
        if(frame_scenario==16)boot_now=0;
        else {boot_regs[1][0x18/4]=2;CHECK(boot_handler(boot_handler_arg)==1);}
    }
    return error;

}
/* Native sysctl tree and user copy are service boundaries. old_sysctl below
 * is imported from the actual fork; callback and wrapper are not mirrored. */
#define SYSCTLFN_ARGS const int *name,unsigned int namelen,void *oldp,size_t *oldlenp,const void *newp,size_t newlen,const int *oname,struct lwp *l,const struct sysctlnode *rnode
#define SYSCTLFN_CALL(n) name,namelen,oldp,oldlenp,newp,newlen,oname,l,n
#define CTLFLAG_READONLY 0
#define CTLFLAG_PRIVATE 0x100
#define CTLFLAG_PERMANENT 0x200
#define CTLTYPE_NODE 1
#define CTLTYPE_STRUCT 5
#define CTL_HW 6
#define CTL_CREATE -3
#define CTL_EOL -1
#define CTL_QUERY -2
#define CTL_DESCRIBE -7
#define SYSCTL_DESCR(s) s
#define __UNCONST(a) ((void *)(uintptr_t)(const void *)(a))
#define SYSCTL_NAMELEN 32
#define SYSCTL_VERSION 0x01000000
#define SYSCTL_VERS_0 0
#define SYSCTL_VERS_1 SYSCTL_VERSION
#define SYSCTL_VERS(f) ((f)&0xff000000)
#define SYSCTL_TYPE(f) ((f)&15)
#define __sysc_pad(type) union {uint64_t __sysc_upad;struct {type __sysc_sdatum;} __sysc_ustr;}
#define __sysc_unpad(x) x.__sysc_ustr.__sysc_sdatum
struct emul {const struct sysctlnode *e_sysctlovly;};
struct proc {const struct emul *p_emul;};
struct lwp {const struct proc *l_proc;};

struct sysctlnode;
typedef int (*sysctlfn)(SYSCTLFN_ARGS);
/* Strict C11 glibc hides these native BSD names. The imported ABI is LP64. */
#if defined(__linux__)
typedef unsigned int u_int;
typedef uint64_t u_quad_t;
#endif
#include "sysctl-node.inc"
_Static_assert(sizeof(struct sysctlnode)==96,"native LP64 sysctlnode layout");

struct sysctllog {unsigned int count;};
static struct sysctlnode frame_nodes[3];
static struct sysctllog frame_log;
static unsigned int frame_node_count,frame_node_calls,frame_teardowns,frame_lookups;
static bool frame_copy_fault,frame_tree_locked,frame_query_churn;
static unsigned int frame_copyins,frame_copyouts,frame_copyout_fault;
static const struct sysctlnode *frame_selected_node;
static int sysctl_createv(struct sysctllog **log,int cflags,const struct sysctlnode **root,
    const struct sysctlnode **out,int flags,int type,const char *label,const char *desc,
    sysctlfn fn,unsigned long long q,void *data,size_t n,...)
{
    (void)desc;CHECK(cflags==0 && log!=NULL && label!=NULL && q==0);
    CHECK((flags&CTLFLAG_PERMANENT)==0 && frame_node_count<3);
    frame_node_calls++;
    if(boot_fault==60+frame_node_calls-1)return ENOMEM;
    if(frame_node_count==0)CHECK(root==NULL && type==CTLTYPE_NODE && data==NULL);
    else CHECK(root!=NULL && *root==&frame_nodes[0] && type==CTLTYPE_STRUCT && data!=NULL);
    struct sysctlnode *node=&frame_nodes[frame_node_count++];
    memset(node,0,sizeof(*node));node->sysctl_flags=flags|type|SYSCTL_VERSION;
    node->sysctl_num=(int)frame_node_count;node->sysctl_data=data;node->sysctl_size=n;node->sysctl_func=fn;
    snprintf(node->sysctl_name,sizeof(node->sysctl_name),"%s",label);node->sysctl_ver=1;node->sysctl_desc=desc;
    if(frame_node_count>1) {node->sysctl_parent=&frame_nodes[0];frame_nodes[0].sysctl_child=&frame_nodes[1];frame_nodes[0].sysctl_clen=frame_node_count-1;}

    CHECK(fn!=NULL);*log=&frame_log;frame_log.count=frame_node_count;if(out)*out=node;
    return 0;
}
static void sysctl_teardown(struct sysctllog **log)
{
    assert(log!=NULL);if(*log==NULL)return;
    CHECK(*log==&frame_log && !frame_tree_locked);
    frame_teardowns++;frame_node_count=frame_log.count=0;*log=NULL;
}
static int sysctl_lookup(SYSCTLFN_ARGS)
{
    (void)name;(void)newlen;(void)oname;(void)l;
    CHECK(namelen==0 && newp==NULL);frame_lookups++;
    if(oldp!=NULL) {
        if(frame_copy_fault)return EFAULT;
        memcpy(oldp,rnode->sysctl_data,*oldlenp<rnode->sysctl_size?*oldlenp:rnode->sysctl_size);
    }
    *oldlenp=rnode->sysctl_size;return 0;
}
static int sysctl_describe(SYSCTLFN_ARGS)
{ (void)name;(void)oldp;(void)oldlenp;(void)newp;(void)newlen;(void)oname;(void)l;CHECK(namelen==1 && rnode==&frame_nodes[0]);return 0; }
static void sysctl_lock(bool write) {(void)write;CHECK(!frame_tree_locked);frame_tree_locked=true;}
static void sysctl_unlock(void) {CHECK(frame_tree_locked);frame_tree_locked=false;}
static int sysctl_dispatch(SYSCTLFN_ARGS)
{ CHECK(frame_tree_locked);(void)rnode;return frame_selected_node->sysctl_func(name,namelen,oldp,oldlenp,newp,newlen,oname,l,frame_selected_node); }
#define KASSERT(x) assert(x)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define rw_lock_held(p) (*(p))
#define sysctl_treelock frame_tree_locked
static int sysctl_copyin(struct lwp *l,const void *src,void *out,size_t n)
{(void)l;frame_copyins++;if(frame_copy_fault)return EFAULT;memcpy(out,src,n);if(frame_query_churn && n==sizeof(struct sysctlnode))((struct sysctlnode *)(uintptr_t)src)->sysctl_ver=1;return 0;}
static int sysctl_copyout(struct lwp *l,const void *src,void *out,size_t n)
{(void)l;frame_copyouts++;if(frame_copy_fault || frame_copyouts==frame_copyout_fault)return EFAULT;memcpy(out,src,n);return 0;}
static const struct sysctlnode *sysctl_rootof(const struct sysctlnode *node)
{while(node->sysctl_parent!=NULL)node=node->sysctl_parent;return node;}
static int sysctl_locate(struct lwp *l,const int *name,unsigned int n,const struct sysctlnode **node,int *index)
{(void)l;(void)name;(void)n;(void)node;(void)index;CHECK(false);return ENOENT;}
static int sysctl_cvt_in(struct lwp *,int *,const void *,size_t,struct sysctlnode *);
static int sysctl_dispatch(SYSCTLFN_ARGS);
/* The unchanged native query uses int ni against uint32_t child count.
 * Our owned tree has at most two children. Keep the native body unchanged. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "old-sysctl.inc"
#pragma GCC diagnostic pop

#undef sysctl_treelock


static int frame_command_actual(struct cixvpu_prepared *p,const struct cixvpu_queue_command *c)
{
    if(frame_trial && frame_scenario==13 && frame_command_calls++==0)return EAGAIN;
    int error=cixvpu_prepared_queue_command(p,c);
    if(frame_trial && frame_scenario==14 && error==0) {
        boot_regs[1][0x18/4]=2;CHECK(boot_handler(boot_handler_arg)==1);
    }
    return error;
}
static int frame_gate_actual(struct cixvpu_prepared *p,bool *out)
{
    /* A legal no-new-observation deferral must not grant stale authority. */
    if(frame_trial && frame_scenario==22 && frame_gate_calls++==0)return EAGAIN;
    return cixvpu_prepared_queue_rpc_pending(p,out);
}
static int frame_copy_actual(struct cixvpu_prepared *p,void *out,size_t n,struct cixvpu_buffer_bitstream_result *r)
{
    int error=cixvpu_prepared_queue_output_copy(p,out,n,r);
    if(frame_trial && frame_scenario==26 && error==0 && (r->flags&0x10)!=0) {
        boot_regs[1][0x18/4]=2;CHECK(boot_handler(boot_handler_arg)==1);
    }
    return error;
}
#define cixvpu_prepared_queue_output_copy frame_copy_actual
#define cixvpu_prepared_queue_command frame_command_actual
#define cixvpu_prepared_queue_rpc_pending frame_gate_actual
#define cixvpu_prepared_boot_rpc_header boot_rpc_header_actual
#define cixvpu_prepared_publish_root boot_publish_actual
#define cixvpu_prepared_boot_rpc_step boot_rpc_step_actual
#include "boot-source.inc"
#undef cixvpu_prepared_queue_output_copy
#undef cixvpu_prepared_queue_command
#undef cixvpu_prepared_queue_rpc_pending
#undef cixvpu_prepared_boot_rpc_step
#undef cixvpu_prepared_publish_root
#undef cixvpu_prepared_boot_rpc_header

static void boot_fixture(void)
{
    profile_fixture(); cold = 0;
    boot_core_bad=0; boot_core_on_fail=boot_core_off_fail=boot_core_no_lease=-1; memset(boot_core_live,0,sizeof(boot_core_live));
    boot_masks = boot_unmasks = boot_intrs = boot_disestablish = 0;
    boot_irq_count = boot_writes = boot_delays = boot_barriers = 0; boot_now = 0; boot_fault = 0;
    boot_handler = NULL; boot_handler_arg = NULL; boot_pic_mask_count = 0;
    boot_term_issued=false;
    boot_rpc_test=false; boot_rpc_commit_events=boot_rpc_notify_events=0;
    boot_rpc_deferrals=0;
    frame_bootstrap_test=false;frame_bootstrap_notifies=0;
    frame_trial=false;frame_scenario=frame_command_calls=frame_gate_calls=frame_fault_acks=0;frame_rpc_faulted=false;
    frame_node_count=frame_node_calls=frame_teardowns=frame_lookups=0;
    frame_copy_fault=frame_tree_locked=frame_query_churn=false;frame_copyins=frame_copyouts=frame_copyout_fault=0;frame_selected_node=NULL;memset(frame_nodes,0,sizeof(frame_nodes));
    memset(boot_regs, 0, sizeof(boot_regs));
    boot_regs[0][0x21c/4] = 0xa5a50ffc;
    boot_regs[0][0x300/4] = 0xf1234567;
    boot_regs[1][0x14/4] = 0x0f0f0f0f;
    boot_regs[1][0x10/4] = UINT32_MAX;
}
/* Missing constructor is a runtime boundary, never a copied implementation. */
#ifndef CIXVPU_BOOT_FRAME_API
static int cixvpu_acpi_boot_create_frame(device_t d,const struct acpi_attach_args *a,
    const struct cixvpu_resources *r,struct cixvpu_acpi_boot **out)
{ (void)d;(void)a;(void)r;(void)out;return ENOTSUP; }
#endif
static void frame_bootstrap_ack(void)
{
    if(boot_fixture_acks()==2)
        boot_fixture_rpc_words(1,2,12,131072,67108864,0,22);
}
static void frame_bootstrap_notified(void)
{
    CHECK(boot_fixture_acks()==2 && boot_fixture_rpc_word(0)==2);
    frame_bootstrap_notifies++;
    if(frame_bootstrap_notifies==1) boot_fixture_rpc_resize(0x20000000,524288);
    else if(frame_bootstrap_notifies==2) boot_fixture_rpc_words(1,2,12,16384,16384,0,12);
    else { CHECK(frame_bootstrap_notifies==3);boot_fixture_rpc(0,0,0); }
}
static void frame_bootstrap_tests(void)
{
    for(unsigned int policy=0;policy<2;policy++) {
        struct cixvpu_acpi_boot *b=NULL;
        boot_fixture();
        int error=policy==1?cixvpu_acpi_boot_create_frame(VPU,&fixture_aa,&attach_description,&b):
            cixvpu_acpi_boot_create_rpc(VPU,&fixture_aa,&attach_description,&b);
        CHECK(error==0);if(error!=0)return;
        frame_bootstrap_test=true;
        boot_actual=boot_fixture_prepare_rpc();b->prepared=boot_actual;
        CHECK(cixvpu_prepared_sync_initial(boot_actual)==0);
        uint32_t root;CHECK(cixvpu_prepared_publish_root(boot_actual,&root)==0);
        b->control.bst=fixture_aa.aa_memt;b->control.bsh=1;
        boot_fixture_runtime(true);boot_fixture_set_ack(frame_bootstrap_ack);
        CHECK(cixvpu_boot_receive(b)==0);
        CHECK(b->report.switched_in && b->report.pong && b->report.messages==2);
        CHECK(frame_bootstrap_notifies==(policy==1?3U:0U));
        CHECK(b->report.rpc_commits==frame_bootstrap_notifies && b->report.rpc_notifications==frame_bootstrap_notifies);
        CHECK(b->report.rpc_second_alloc_replied==(policy==1));
        boot_fixture_drop(boot_actual);
#ifdef CIXVPU_BOOT_FRAME_API
        if(b->frame_result!=NULL) {
            sysctl_teardown(&b->frame_log);cixvpu_frame_claimed=0;AcpiOsFree(b->frame_result);
        }
#endif
        AcpiOsFree(b);
    }
}
static void frame_trial_tests(void)
{
    for(unsigned int scenario=0;scenario<2;scenario++) {
        struct cixvpu_acpi_boot *b=NULL;
        boot_fixture();frame_trial=true;frame_scenario=scenario;
        CHECK(cixvpu_acpi_boot_create_frame(VPU,&fixture_aa,&attach_description,&b)==0);assert(b!=NULL);
        boot_actual=boot_fixture_prepare_frame();struct cixvpu_prepared *owner=boot_actual;
        CHECK(cixvpu_acpi_boot_once(b,&owner)==0 && owner==NULL);
        CHECK(boot_fixture_frame_publications()==13);
        CHECK(b->report.exposed && b->report.held && boot_disestablish==0 && boot_masks==1);
        CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
        boot_fixture_drop(boot_actual);sysctl_teardown(&b->frame_log);cixvpu_frame_claimed=0;
        AcpiOsFree(b->frame_result);AcpiOsFree(b);
    }
}
static void frame_advisory_tests(void)
{
    struct cixvpu_acpi_boot b;memset(&b,0,sizeof(b));
    const enum cixvpu_command_response_kind kinds[]={CIXVPU_COMMAND_JOB_DEQUEUED,CIXVPU_COMMAND_STATE};
    for(unsigned int i=0;i<2;i++) {
        struct cixvpu_queue_event event;memset(&event,0,sizeof(event));
        event.kind=CIXVPU_Q_RESPONSE;event.response.kind=kinds[i];
        bool waiting=false,idle=false;unsigned int pending=0,n=0;
        CHECK(cixvpu_frame_event(&b,&event,&waiting,&pending,&idle,&n)==0);
        CHECK(n==8 && !b.report.frame_complete && !waiting && !idle && pending==0);
    }
}
static void frame_fault_tests(void)
{
    const struct {unsigned int scenario;int error;} cases[]={
        {2,EPROTO},{3,ETIMEDOUT},{4,EIO},{5,EIO},{6,ENOTSUP},
        {7,EPROTO},{8,EIO},{9,EIO},{10,EPROTO},{11,ENOTSUP},{12,EPROTO},
        {13,0},{14,EIO},{15,EIO},{16,EIO},{17,EIO},{20,0},{21,E2BIG},{22,0},{23,E2BIG},{24,0},{25,E2BIG},{26,EIO},{27,0},{28,0},{29,EPROTO},{30,ETIMEDOUT}
    };
    for(unsigned int i=0;i<__arraycount(cases);i++) {
        boot_fixture();frame_trial=true;frame_scenario=cases[i].scenario;
        struct cixvpu_acpi_boot *b=NULL;
        CHECK(cixvpu_acpi_boot_create_frame(VPU,&fixture_aa,&attach_description,&b)==0);assert(b!=NULL);
        boot_actual=boot_fixture_prepare_frame();struct cixvpu_prepared *owner=boot_actual;
        int error=cixvpu_acpi_boot_once(b,&owner);
        if(error!=cases[i].error)fprintf(stderr,"frame scenario %u got %d expected %d\n",frame_scenario,error,cases[i].error);
        CHECK(error==cases[i].error && b->report.error==error && owner==NULL);
        CHECK(b->report.held && b->report.exposed && boot_masks==1 && boot_disestablish==0);
        CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
        CHECK(b->report.frame_complete==(error==0));
        CHECK(b->report.frame_publications<=32 && b->report.frame_notifications<=32);
        CHECK(b->report.frame_messages<=128 && b->report.frame_output_returns<=4);
        CHECK(b->report.frame_output_bytes<=262144 && b->report.frame_received_bytes<=65536);
        if(frame_scenario==20 || frame_scenario==22) {
            CHECK(b->report.frame_publications==32 && b->report.frame_notifications==32);
            CHECK(b->report.frame_complete && b->report.frame_output_bytes==8 && boot_delays>=10);
            CHECK(b->report.rpc_commits==19 && b->report.rpc_notifications==19);
        }
        if(frame_scenario==21) {
            CHECK(b->report.frame_publications==32 && b->report.rpc_commits==19);
            CHECK(boot_fixture_rpc_word(0)==1 && !b->report.frame_complete);
        }
        if(frame_scenario==14)CHECK(b->report.frame_publications==1 && b->report.frame_notifications==0 && boot_fixture_frame_publications()==0);
        if(frame_scenario>=15 && frame_scenario<=17) {
            CHECK(frame_rpc_faulted && boot_fixture_acks()==frame_fault_acks);
            CHECK(!b->report.frame_input_accepted && b->report.frame_output_bytes==0);
        }
        if(frame_scenario==3 || frame_scenario==25) {
            frame_selected_node=&frame_nodes[1];size_t count=0;
            CHECK(old_sysctl(NULL,0,NULL,&count,NULL,0,NULL)==(frame_scenario==3?ENODATA:0));
            CHECK(count==b->report.frame_output_bytes);
            frame_selected_node=&frame_nodes[2];uint8_t status[96];count=sizeof(status);
            CHECK(old_sysctl(NULL,0,status,&count,NULL,0,NULL)==0 && count==96);
            CHECK(status[0]==1 && status[8]==(uint8_t)error && status[9]==0 && status[10]==0 && status[11]==0 && status[28]==0);
        }
        if(frame_scenario==30)CHECK(b->report.frame_visits==5001 && b->report.frame_publications==2 && boot_delays==4999);
        if(frame_scenario==28)CHECK(boot_delays>=10 && boot_fixture_frame_publications()==13);
        if(frame_scenario==23)CHECK(b->report.frame_messages==128);
        if(frame_scenario==24)CHECK(b->report.frame_complete && b->report.frame_output_bytes==262144 && b->report.frame_output_returns==4);
        if(frame_scenario==25)CHECK(!b->report.frame_complete && b->report.frame_output_returns==4 && boot_fixture_frame_publications()==15);
        boot_fixture_drop(boot_actual);sysctl_teardown(&b->frame_log);cixvpu_frame_claimed=0;
        AcpiOsFree(b->frame_result);AcpiOsFree(b);
    }
}
static struct cixvpu_acpi_boot *boot_new(bool special)
{
    struct cixvpu_acpi_boot *ctx = NULL;
    CHECK(cixvpu_acpi_boot_create(VPU, &fixture_aa, &attach_description, &ctx) == 0);
    assert(ctx != NULL);
    boot_actual = boot_fixture_prepare_kind(special);
    assert(boot_actual != NULL);
    return ctx;
}
static void frame_export_tests(void)
{
    (void)sysctl_createv;(void)sysctl_teardown;(void)sysctl_lookup;
    (void)sysctl_query;(void)sysctl_describe;
    struct cixvpu_acpi_boot *b=NULL;
    boot_fixture();
    CHECK(cixvpu_acpi_boot_create_frame(VPU,&fixture_aa,&attach_description,&b)==0);assert(b!=NULL);
    CHECK(frame_node_count==3);
    if(frame_node_count!=3) {AcpiOsFree(b->frame_result);AcpiOsFree(b);return;}
    struct cixvpu_acpi_boot *other=NULL;unsigned int baseline=live;
    CHECK(cixvpu_acpi_boot_create_frame(VPU,&fixture_aa,&attach_description,&other)==EBUSY);
    CHECK(other==NULL && live==baseline && frame_node_count==3 && frame_teardowns==0);
    frame_selected_node=&frame_nodes[0];int query=CTL_QUERY;
    struct sysctlnode request;memset(&request,0,sizeof(request));request.sysctl_flags=SYSCTL_VERSION;
    struct sysctlnode discovered[2];memset(discovered,0,sizeof(discovered));size_t metadata=sizeof(discovered);
    CHECK(old_sysctl(&query,1,discovered,&metadata,&request,sizeof(request),NULL)==0);
    CHECK(metadata==sizeof(discovered) && frame_lookups==0);
    CHECK(strcmp(discovered[0].sysctl_name,"hevc_frame")==0 && strcmp(discovered[1].sysctl_name,"frame_status")==0);
    for(unsigned int i=0;i<2;i++) {
        CHECK(discovered[i].sysctl_data==NULL && discovered[i].sysctl_func==NULL && discovered[i].sysctl_parent==NULL && discovered[i].sysctl_desc==NULL);
        CHECK(discovered[i].__rsvd==0 && discovered[i].sysctl_ver==1 && discovered[i].sysctl_num==(int)i+2);
        CHECK(discovered[i].sysctl_flags==frame_nodes[i+1].sysctl_flags && discovered[i].sysctl_size==frame_nodes[i+1].sysctl_size);
    }
    /* The same current-version request shape is used by libc __learn_tree. */
    struct emul native_emul={NULL};struct proc process={&native_emul};struct lwp user={&process};
    request.sysctl_ver=1;metadata=sizeof(discovered);
    CHECK(old_sysctl(&query,1,discovered,&metadata,&request,sizeof(request),&user)==ENOTSUP);
    request.sysctl_ver=UINT32_MAX;
    CHECK(old_sysctl(&query,1,NULL,&metadata,&request,sizeof(request),&user)==ENOTSUP);
    request.sysctl_ver=0;frame_copyins=0;frame_query_churn=true;
    CHECK(old_sysctl(&query,1,discovered,&metadata,&request,sizeof(request),&user)==0);
    CHECK(frame_copyins==1 && request.sysctl_ver==1);
    frame_query_churn=false;request.sysctl_ver=0;
    /* Compare normal version-zero discovery to the unchanged native engine. */
    sysctlfn handler=frame_nodes[0].sysctl_func;frame_nodes[0].sysctl_func=sysctl_query;
    struct sysctlnode native_nodes[2];size_t native_size=sizeof(native_nodes);
    CHECK(old_sysctl(&query,1,native_nodes,&native_size,&request,sizeof(request),NULL)==0 && native_size==sizeof(native_nodes));
    for(unsigned int i=0;i<2;i++) {
        CHECK(native_nodes[i].sysctl_num==discovered[i].sysctl_num && native_nodes[i].sysctl_flags==discovered[i].sysctl_flags);
        CHECK(native_nodes[i].sysctl_size==discovered[i].sysctl_size && strcmp(native_nodes[i].sysctl_name,discovered[i].sysctl_name)==0);
        CHECK(native_nodes[i].sysctl_data!=NULL && native_nodes[i].sysctl_func!=NULL);
    }
    native_size=sizeof(native_nodes)-1;
    CHECK(old_sysctl(&query,1,native_nodes,&native_size,&request,sizeof(request),NULL)==ENOMEM && native_size==sizeof(native_nodes));
    frame_nodes[0].sysctl_func=handler;
    frame_copyouts=0;frame_copyout_fault=2;metadata=sizeof(discovered);
    memset(discovered,0xa5,sizeof(discovered));
    CHECK(old_sysctl(&query,1,discovered,&metadata,&request,sizeof(request),&user)==EFAULT && metadata==sizeof(discovered));
    CHECK(frame_copyouts==2 && discovered[0].sysctl_data==NULL && ((const uint8_t *)&discovered[1])[0]==0xa5);
    frame_copyout_fault=0;

    request.sysctl_ver=0;native_emul.e_sysctlovly=&frame_nodes[0];
    CHECK(old_sysctl(&query,1,NULL,&metadata,&request,sizeof(request),&user)==ENOTSUP);
    native_emul.e_sysctlovly=NULL;
    metadata=sizeof(discovered)-1;
    CHECK(old_sysctl(&query,1,discovered,&metadata,&request,sizeof(request),NULL)==ENOMEM && metadata==sizeof(discovered));
    request.sysctl_flags=0;metadata=sizeof(discovered);

    CHECK(old_sysctl(&query,1,discovered,&metadata,&request,sizeof(request),NULL)==EINVAL);
    request.sysctl_flags=SYSCTL_VERSION;
    CHECK(old_sysctl(&query,1,NULL,&metadata,&request,1,NULL)==EINVAL);
    frame_copy_fault=true;CHECK(old_sysctl(&query,1,NULL,&metadata,&request,sizeof(request),NULL)==EFAULT);frame_copy_fault=false;
    query=CTL_DESCRIBE;metadata=0;CHECK(old_sysctl(&query,1,NULL,&metadata,NULL,0,NULL)==0 && frame_lookups==0);
    CHECK(old_sysctl(&query,1,NULL,&metadata,&request,sizeof(request),NULL)==EPERM);
    query=1;CHECK(old_sysctl(&query,1,NULL,&metadata,NULL,0,NULL)==EINVAL && frame_lookups==0);
    frame_selected_node=&frame_nodes[1];
    uint8_t copy[96];memset(copy,0xa5,sizeof(copy));size_t n=sizeof(copy);int name=CTL_QUERY;
    CHECK(old_sysctl(NULL,0,copy,&n,NULL,0,NULL)==EAGAIN && n==sizeof(copy) && copy[0]==0xa5);
    CHECK(old_sysctl(NULL,0,NULL,&n,copy,1,NULL)==EPERM);
    unsigned int lookups=frame_lookups;
    CHECK(old_sysctl(&name,1,copy,&n,NULL,0,NULL)==EINVAL && frame_lookups==lookups);
    name=CTL_DESCRIBE;CHECK(old_sysctl(&name,1,copy,&n,NULL,0,NULL)==EINVAL);
    name=1;CHECK(old_sysctl(&name,1,copy,&n,NULL,0,NULL)==EINVAL);
    frame_trial=true;boot_actual=boot_fixture_prepare_frame();struct cixvpu_prepared *owner=boot_actual;
    CHECK(cixvpu_acpi_boot_once(b,&owner)==0);
    unsigned int writes=boot_writes;
    n=0;CHECK(old_sysctl(NULL,0,NULL,&n,NULL,0,NULL)==0 && n==8);
    n=4;CHECK(old_sysctl(NULL,0,copy,&n,NULL,0,NULL)==ENOMEM && n==8);
    n=4;CHECK(frame_selected_node->sysctl_func(NULL,0,copy,&n,NULL,0,NULL,NULL,frame_selected_node)==0 && n==8);
    frame_copy_fault=true;n=4;memset(copy,0xa5,sizeof(copy));
    CHECK(old_sysctl(NULL,0,copy,&n,NULL,0,NULL)==EFAULT && n==4 && copy[0]==0xa5);
    frame_copy_fault=false;n=sizeof(copy);
    CHECK(old_sysctl(NULL,0,copy,&n,NULL,0,NULL)==0 && n==8);
    const uint8_t expected[]={0,0,1,0x26,1,2,3,4};CHECK(memcmp(copy,expected,8)==0);
    frame_selected_node=&frame_nodes[2];n=sizeof(copy);
    CHECK(old_sysctl(NULL,0,copy,&n,NULL,0,NULL)==0 && n==96);
    CHECK(copy[0]==1 && copy[4]==96 && copy[8]==0 && copy[28]==1 && copy[32]==8 && copy[36]==2);
    CHECK(boot_writes==writes && !frame_tree_locked && b->report.exposed && boot_disestablish==0);
#ifdef CIXVPU_BOOT_FRAME_EXPORT_API
    sysctl_teardown(&b->frame_log);cixvpu_frame_claimed=0;
#endif
    boot_fixture_drop(boot_actual);AcpiOsFree(b->frame_result);AcpiOsFree(b);
    for(unsigned int i=0;i<4;i++) {
        boot_fixture();boot_fault=60+i;b=NULL;unsigned int baseline=live;
        CHECK(cixvpu_acpi_boot_create_frame(VPU,&fixture_aa,&attach_description,&b)==ENOMEM);
        CHECK(b==NULL && live==baseline && frame_node_count==0 && boot_writes==0 && maps==0);
    }
}

static void boot_rpc_request(void)
{
    if(boot_fault==47) { boot_fixture_rpc_words(1,2,12,16384,16384,0,12); return; }
    if(boot_fault==40) { boot_fixture_rpc_resize(0x20000000,524288); return; }
    if(boot_fault==30 || boot_fault==31 || boot_fault==44 || boot_fault==52 || boot_fault==53)
        boot_fixture_rpc_words(1,1,1012,0,0,0,0);
    else
        boot_fixture_rpc_words(1,2,12,131072,boot_fault==35 ? 67108865 : 67108864,0,22);
}
static void boot_rpc_commit_hook(void)
{
    boot_rpc_commit_events++;
    CHECK(boot_fixture_rpc_word(0)==2 && boot_fixture_acks()==0);
    if(boot_fault==32 || (boot_fault==42 && boot_rpc_commit_events==2) || (boot_fault==50 && boot_rpc_commit_events==3)) {
        boot_regs[1][0x18/4]=2; CHECK(boot_handler(boot_handler_arg)==1);
    }
}
static void boot_rpc_notified(void)
{
    CHECK(boot_rpc_commit_events==boot_rpc_notify_events+1);
    CHECK(boot_fixture_rpc_word(0)==2 && boot_fixture_acks()==0);
    if(boot_fixture_rpc_word(1)==2 || boot_fixture_rpc_word(1)==3) {
        CHECK(boot_fixture_rpc_word(2)==4 && boot_fixture_rpc_word(3)==(boot_fixture_rpc_word(4)==16384 ? 0x24000000U : 0x20000000U));
    } else CHECK(boot_fixture_rpc_word(1)==1);
    boot_rpc_notify_events++;
    if(boot_fault==27) return; /* RETURN must not notify twice. */
    if(boot_fault>=45 && boot_fault<=53) {
        unsigned int first=boot_fault>=52 ? 17 : 1;
        if(boot_fault>=52 && boot_rpc_notify_events<16) { boot_rpc_request(); return; }
        if(boot_fault>=52 && boot_rpc_notify_events==16) {
            boot_fixture_rpc_words(1,2,12,131072,67108864,0,22); return;
        }
        if(boot_rpc_notify_events==first && boot_fault!=48) {
            boot_fixture_rpc_resize(0x20000000,524288); return;
        }
        if(boot_rpc_notify_events==first+1 || boot_fault==48 || boot_fault==46) {
            boot_fixture_rpc_words(1,2,12,boot_fault==49 ? 16383 : 16384,16384,0,12); return;
        }
        if(boot_fault==53) { boot_rpc_request(); return; }
    }
    if(boot_fault>=38 && boot_fault<=43) {

        if(boot_rpc_notify_events==1 || boot_fault==39) {
            boot_fixture_rpc_resize(0x20000000,boot_fault==41 ? 524287 : 524288); return;
        }
    }
    if(boot_fault==44) {
        if(boot_rpc_notify_events<16) { boot_rpc_request(); return; }
        if(boot_rpc_notify_events==16) { boot_fixture_rpc_words(1,2,12,131072,67108864,0,22); return; }
        if(boot_rpc_notify_events==17) { boot_fixture_rpc_resize(0x20000000,524288); return; }
    }

    if(boot_fault==28) { boot_rpc_request(); return; }
    if(boot_fault==37) { boot_fixture_rpc_resize(0x89abcdef,0xfedcba98); return; }
    if(boot_fault==29 || boot_fault==34) {
        boot_fixture_rpc_words(1,boot_fault==29 ? 3 : 4,12,131072,67108864,0,22); return;
    }
    if(boot_fault==30 && boot_rpc_notify_events<16) { boot_rpc_request(); return; }
    if(boot_fault==31) { boot_rpc_request(); return; }
    if(boot_fault==30 && boot_rpc_notify_events==16) {
        boot_fixture_rpc_words(1,2,12,131072,67108864,0,22); return;
    }
    /* Device changes stale fields after consuming RETURN, before queue input. */
    boot_fixture_rpc_words(0,0xdeadbeef,UINT32_MAX,0,0,0,0);
    boot_fixture_runtime(true);
}
static void boot_rpc_integration_tests(void)
{
    for(unsigned int scenario=26;scenario<=53;scenario++) {
        struct cixvpu_acpi_boot *ctx=NULL;
        struct cixvpu_prepared *owned;
        struct cixvpu_boot_report report={0};
        boot_fixture(); boot_fault=scenario; boot_rpc_test=true;
        CHECK(cixvpu_acpi_boot_create_rpc(VPU,&fixture_aa,&attach_description,&ctx)==0);
        boot_actual=boot_fixture_prepare_rpc(); owned=boot_actual;
        int expected=scenario==26 || scenario==30 || scenario==36 || scenario==38 || scenario==44 || scenario==45 || scenario==52 ? 0 : scenario==27 ? ETIMEDOUT :
            scenario==32 || scenario==33 || scenario==42 || scenario==43 || scenario==50 || scenario==51 ? EIO : scenario==28 || scenario==39 || scenario==46 ? EBUSY :
            scenario==31 || scenario==53 ? E2BIG : scenario==29 ? EPROTO : ENOTSUP;
        int actual=cixvpu_acpi_boot_once(ctx,&owned);
        CHECK(actual==expected && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&report)==0 && report.error==expected);
        if(actual!=expected) fprintf(stderr,"RPC scenario %u: result %d expected %d, replies %u notifications %u messages %u visits %u\n",
            scenario,actual,expected,report.rpc_commits,report.rpc_notifications,report.messages,report.visits);
        unsigned int commits=scenario==52 || scenario==53 ? 19 : scenario==45 || scenario==46 || scenario==50 || scenario==51 ? 3 : scenario==47 ? 0 : scenario==49 ? 2 : scenario==44 ? 18 : scenario==30 ? 17 : scenario==31 ? 16 : scenario==35 || scenario==40 ? 0 : scenario==38 || scenario==39 || scenario==42 || scenario==43 ? 2 : 1;
        CHECK(boot_rpc_commit_events==commits && report.rpc_commits==commits);
        CHECK(boot_rpc_notify_events==(scenario==32 || scenario==42 || scenario==50 ? commits-1 : commits));
        CHECK(report.rpc_notifications==boot_rpc_notify_events);
        CHECK(report.rpc_alloc_replied==(scenario!=31 && scenario!=35 && scenario!=40 && scenario!=47));
        CHECK(report.rpc_resize_replied==(scenario==38 || scenario==39 || scenario==42 || scenario==43 || scenario==44 || scenario==45 || scenario==46 || scenario>=49));
        CHECK(report.rpc_second_alloc_replied==(scenario==45 || scenario==46 || scenario==50 || scenario==51 || scenario==52 || scenario==53));
        CHECK(report.exposed && report.held && report.prepared_retained && report.irq_masked);
        CHECK(report.irq_registered && boot_disestablish==0 && boot_masks==1);
        CHECK(report.messages==(expected==0 ? 2U : 0U));
        CHECK(boot_fixture_acks()==report.messages);
        CHECK(report.switched_in==(expected==0) && report.pong==(expected==0));
        if(scenario==32 || scenario==33 || scenario==42 || scenario==43 || scenario==50 || scenario==51) CHECK(report.irq_unexpected==2);
        if(scenario==35) CHECK(report.rpc_alloc_valid && report.rpc_alloc_max_size==67108865);
        if(scenario==27) CHECK(report.visits>1 && boot_rpc_notify_events==1);
        if(scenario==36) CHECK(report.visits>=2 && boot_rpc_deferrals>=2 && boot_delays>=1);
        if(scenario==37) {
            CHECK(report.rpc_header_valid && report.rpc_state==1 && report.rpc_callid==3 && report.rpc_size==8);
            CHECK(report.rpc_resize_valid && report.rpc_resize_address==0x89abcdef && report.rpc_resize_new_size==0xfedcba98);
            CHECK(report.rpc_header_error==0 && report.last_stage==CIXVPU_BOOT_RECEIVE);
        } else if(scenario==39 || (scenario>=40 && scenario<=43)) {
            CHECK(report.rpc_resize_valid && report.rpc_resize_address==0x20000000 && report.rpc_resize_new_size==(scenario==41 ? 524287U : 524288U));
        } else CHECK(!report.rpc_resize_valid && report.rpc_resize_address==0 && report.rpc_resize_new_size==0);
        unsigned int writes=boot_writes;
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EALREADY && boot_writes==writes);
        CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
        boot_fixture_drop(boot_actual); AcpiOsFree(ctx);
    }
}
static void boot_rpc_policy_tests(void)
{
    struct cixvpu_acpi_boot *ctx=NULL;
    struct cixvpu_prepared *owned;
    struct cixvpu_boot_report report={0};

    boot_fixture();
    CHECK(cixvpu_acpi_boot_create_rpc(NULL,&fixture_aa,&attach_description,&ctx)==EINVAL);
    CHECK(cixvpu_acpi_boot_create_rpc(VPU,NULL,&attach_description,&ctx)==EINVAL);
    CHECK(cixvpu_acpi_boot_create_rpc(VPU,&fixture_aa,NULL,&ctx)==EINVAL);
    CHECK(cixvpu_acpi_boot_create_rpc(VPU,&fixture_aa,&attach_description,NULL)==EINVAL);
    CHECK(ctx==NULL && maps==0 && nevents==0);
    interrupt_context=true;
    CHECK(cixvpu_acpi_boot_create_rpc(VPU,&fixture_aa,&attach_description,&ctx)==EWOULDBLOCK);
    interrupt_context=false; soft_context=true;
    CHECK(cixvpu_acpi_boot_create_rpc(VPU,&fixture_aa,&attach_description,&ctx)==EWOULDBLOCK);
    soft_context=false;
    CHECK(cixvpu_acpi_boot_create_rpc(VPU,&fixture_aa,&attach_description,&ctx)==0);
    CHECK(ctx!=NULL && ctx->rpc_enabled && maps==0 && nevents==0);
    CHECK(cixvpu_acpi_boot_create_rpc(VPU,&fixture_aa,&attach_description,&ctx)==EBUSY);
    boot_actual=boot_fixture_prepare(); owned=boot_actual;
    /* A new RPC coordinator must not service an ordinary plan0 owner. */
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&report)==0);
    CHECK(report.exposed && report.held && report.irq_masked && report.prepared_retained);
    CHECK(report.error==ENOTSUP && report.messages==0 && boot_fixture_acks()==0);
    CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
    boot_fixture_drop(boot_actual); AcpiOsFree(ctx);
}
static void boot_discard_fixture(struct cixvpu_acpi_boot *ctx)
{
    /* End assertions before host-only quarantine teardown; no driver bypass. */
    if (boot_fixture_live()!=0) boot_fixture_drop(boot_actual);
    CHECK(boot_fixture_live()==0);
    AcpiOsFree(ctx);
}
static void boot_failure_tests(void)
{
    struct cixvpu_acpi_boot *ctx;
    struct cixvpu_prepared *owned;
    struct cixvpu_boot_report r = {0};
    unsigned int before;

    /* Real RPC snapshot rejection retains the one-copy raw header, not payload. */
    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=23;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(r.rpc_header_valid && r.rpc_state==1 && r.rpc_callid==0x12345678 && r.rpc_size==0xfedcba98);
    CHECK(r.rpc_header_error==0 && r.error==ENOTSUP && r.last_stage==CIXVPU_BOOT_RECEIVE);
    CHECK(r.visits==1 && r.messages==0 && !r.switched_in && !r.pong);
    CHECK(r.exposed && r.prepared_retained && r.held && r.irq_registered && r.irq_masked);
    CHECK(boot_fixture_acks()==0 && boot_masks==1 && boot_disestablish==0);
    CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
    boot_discard_fixture(ctx);

    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=24;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(r.error==ENOTSUP && r.rpc_header_error==EIO && !r.rpc_header_valid);
    CHECK(r.rpc_state==0 && r.rpc_callid==0 && r.rpc_size==0 && r.messages==0);
    CHECK(!r.rpc_alloc_valid && r.rpc_alloc_size==0 && r.rpc_alloc_max_size==0 &&
        r.rpc_alloc_region==0 && r.rpc_alloc_log2_alignment==0);
    CHECK(r.last_stage==CIXVPU_BOOT_RECEIVE && r.held && r.irq_masked && boot_fixture_acks()==0);
    boot_discard_fixture(ctx);

    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=25;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(r.rpc_header_valid && r.rpc_state==1 && r.rpc_callid==2 && r.rpc_size==12);
    CHECK(r.rpc_alloc_valid && r.rpc_alloc_size==0x12345678 && r.rpc_alloc_max_size==0xfedcba98);
    CHECK(r.rpc_alloc_region==1 && r.rpc_alloc_log2_alignment==31);
    CHECK(r.error==ENOTSUP && r.rpc_header_error==0 && r.last_stage==CIXVPU_BOOT_RECEIVE);
    CHECK(r.visits==1 && r.messages==0 && !r.switched_in && !r.pong);
    CHECK(r.exposed && r.prepared_retained && r.held && r.irq_registered && r.irq_masked);
    CHECK(boot_fixture_acks()==0 && boot_masks==1 && boot_disestablish==0);
    CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
    boot_discard_fixture(ctx);

    /* Real ordinary constructor: root refuses at its actual typed boundary. */
    boot_fixture(); ctx=boot_new(false); owned=boot_actual; boot_fault=6;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(!r.exposed && r.cleanup_error==EIO && r.prepared_retained);
    CHECK(boot_pic_mask_count==1 && boot_masks==1 && ctx->irq_ready==0);
    CHECK(ctx->control.mapped && ctx->control.profile_mapped && power_live);
    CHECK(boot_disestablish==0 && boot_unmasks==1);
    boot_discard_fixture(ctx);

    /* Actual final HOST cursor PREWRITE publishes an asynchronous IRQ fault. */
    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=9;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(r.exposed && r.switched_in && r.pong && r.irq_unexpected==2);
    CHECK(r.error==EIO && r.irq_seen==1 && r.irq_masked);
    CHECK(boot_regs[1][0x18/4]==2);
    CHECK(boot_handler(boot_handler_arg)==0 && boot_masks==1);
    CHECK(boot_masks==1 && boot_disestablish==0 && ctx->control.mapped);
    CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
    boot_discard_fixture(ctx);

    /* Fault after actual capability success but before its caller resumes. */
    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=8;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(r.exposed && r.prepared_retained && r.irq_registered && r.irq_masked);
    CHECK(boot_writes>0 && boot_regs[1][0x204/4]==0 && boot_disestablish==0);
    CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY);
    boot_discard_fixture(ctx);

    /* Repeated sync is rejected by the real allocator before hardware/MOVE. */
    boot_fixture(); ctx=boot_new(true); owned=boot_actual;
    CHECK(cixvpu_prepared_sync_initial(owned)==0);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EBUSY && owned==boot_actual);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
    CHECK(r.started && !r.moved && !r.exposed && !r.irq_registered);
    CHECK(nevents==0 && maps==0 && boot_writes==0 && boot_intrs==0);
    before=boot_writes; CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EALREADY);
    CHECK(owned==boot_actual && boot_writes==before);
    boot_discard_fixture(ctx);
}
static void boot_matrix_tests(void)
{
    struct cixvpu_acpi_boot *ctx;
    struct cixvpu_prepared *owned;
    struct cixvpu_boot_report r = {0};
    int error;
    const unsigned int setup_faults[]={F_CLOCK_GET,F_RESET_GET,F_RCSU_GET,
        F_RCSU_STATE,F_POWER_ON,F_STA_ON,F_STA_ON_FAIL,F_CLOCK_ON,
        F_RESET_ASSERT,F_RESET_DEASSERT,F_MAP,F_PROFILE_MAP,F_NATIVE_POISON,
        F_POWER_NO_LEASE};
    for(unsigned int i=0;i<sizeof(setup_faults)/sizeof(setup_faults[0]);i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; faults=setup_faults[i];
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.started && r.moved && !r.exposed);
        CHECK(boot_writes==0 && boot_unmasks==0);
        CHECK(!r.irq_registered || r.irq_masked);
        CHECK(r.prepared_retained == (r.cleanup_error!=0));
        if(r.cleanup_error!=0) CHECK(r.held && boot_fixture_live()!=0);
        else CHECK(!r.held && boot_fixture_live()==0 && r.control.released);
        boot_discard_fixture(ctx);
    }
    const unsigned int metadata_bad[]={1,7,10,16,25,26,29,39,42};
    for(unsigned int i=0;i<sizeof(metadata_bad)/sizeof(metadata_bad[0]);i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; bad=metadata_bad[i];
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(nevents==0 && boot_intrs==0 && maps==0 && boot_writes==0);
        CHECK(!r.prepared_retained && boot_fixture_live()==0);
        boot_discard_fixture(ctx);
    }
    for(unsigned int i=1;i<=3;i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_core_bad=i;
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(boot_intrs==0 && maps==0 && boot_writes==0 && !r.prepared_retained);
        boot_discard_fixture(ctx);
    }
    for(unsigned int i=0;i<4;i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_core_on_fail=(int)i;
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(r.core_attempted==((1U<<(i+1))-1) && r.core_released==r.core_attempted);
        CHECK(boot_writes==1 && boot_unmasks==0 && boot_disestablish==1);
        for(unsigned int j=0;j<4;j++) CHECK(!boot_core_live[j]);
        /* Exact reverse OFF sequence follows the checked reset/clock stop. */
        char expected[8]; unsigned int n=0;
        for(int j=(int)i;j>=0;j--) expected[n++]=(char)('4'+j);
        expected[n]=0; CHECK(strstr(events,expected)!=NULL);
        boot_discard_fixture(ctx);
    }
    const unsigned int cleanup_faults[]={F_RESET_ASSERT,F_CLOCK_OFF,F_POWER_OFF,
        F_STA_OFF,F_STA_OFF_FAIL,F_PUT_MAIN,F_PUT_RCSU,F_PUT_CLOCK};
    for(unsigned int i=0;i<sizeof(cleanup_faults)/sizeof(cleanup_faults[0]);i++) {
        boot_fixture(); ctx=boot_new(false); owned=boot_actual;
        /* Inject only after successful setup/unmask, at the real PIC boundary. */
        boot_fault=13+i;
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error!=0);
        CHECK(r.prepared_retained && r.held && r.irq_masked);
        CHECK(ctx->irq_ready==0 && boot_masks==1);
        CHECK((i==0 && boot_disestablish==0) || (i!=0 && boot_disestablish==1));
        boot_discard_fixture(ctx);
    }
    for(unsigned int i=0;i<4;i++) {
        boot_fixture(); ctx=boot_new(false); owned=boot_actual; boot_core_off_fail=(int)i;
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)==ENOTSUP && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.cleanup_error==EIO && !r.exposed);
        CHECK(r.core_attempted==15 && boot_core_live[i] && !r.control.released);
        CHECK((r.core_released & (1U<<i))==0 && r.prepared_retained);
        CHECK(ctx->control.mapped && ctx->control.profile_mapped && power_live);
        boot_discard_fixture(ctx);
    }
    for(unsigned int mode=2;mode<=12;mode++) {
        if(mode==6 || mode==8 || mode==9) continue;
        boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=mode;
        error=cixvpu_acpi_boot_once(ctx,&owned);
        CHECK(error!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0);
        if(mode==2 || mode==3 || mode==4 || mode==11 || mode==12) {
            CHECK(!r.exposed && !r.prepared_retained && r.cleanup_error==0);
            CHECK(boot_fixture_live()==0 && r.control.released);
        } else {
            CHECK(r.exposed && r.prepared_retained && r.held && r.irq_masked);
            CHECK(cixvpu_prepared_free(&boot_actual)==EBUSY && boot_disestablish==0);
        }
        if(mode==5) CHECK(r.visits==1001 && error==ETIMEDOUT && boot_now==0);
        if(mode==7) CHECK(r.visits>0 && r.visits<=1001 && error==ETIMEDOUT);
        if(mode==4) CHECK(r.terminate_reads>0 && r.terminate_reads<=10001 && error==ETIMEDOUT);
        if(mode==12) CHECK(r.terminate_reads==10001 && error==ETIMEDOUT && boot_now==0);
        if(mode==11) CHECK(r.terminate_reads==1 && error==EIO);
        if(mode==10) CHECK(boot_regs[1][1]==0 && error==EBUSY);
        boot_discard_fixture(ctx);
    }
    for(unsigned int i=0;i<7;i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual;
        switch(i) {
        case 0: boot_regs[1][1]=2; break;
        case 1: boot_regs[1][0x10/4]=0xfffffffe; break;
        case 2: boot_regs[1][0x18/4]=2; break;
        case 3: boot_regs[1][0x20c/4]=1; break;
        case 4: boot_regs[1][0x214/4]=1; break;
        case 5: boot_regs[1][0x14/4]=0x0f0f000f; break;
        case 6: boot_regs[1][0x218/4]=1; break;
        }
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EBUSY && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(boot_writes==0 && boot_unmasks==0 && boot_fixture_live()==0);
        boot_discard_fixture(ctx);
    }
    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fixture_map_valid(owned,false);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==boot_actual);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.started && !r.moved && !r.exposed);
    CHECK(nevents==0 && boot_intrs==0 && maps==0 && boot_writes==0);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EALREADY);
    boot_fixture_map_valid(owned,true);
    boot_discard_fixture(ctx);

    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_core_no_lease=2;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==EIO);
    CHECK(r.core_attempted==7 && r.core_released==0 && r.prepared_retained && r.held);
    CHECK(boot_core_live[0] && boot_core_live[1] && !boot_core_live[2]);
    CHECK(r.core_status[2]==AE_NOT_FOUND && r.core_cleanup[2]==AE_ABORT_METHOD);
    CHECK(ctx->control.mapped && power_live && clock_claim.claimed);
    boot_discard_fixture(ctx);
}
static void boot_trace_api_tests(void)
{
    struct cixvpu_acpi_boot *ctx=NULL;
    struct cixvpu_prepared *owned=NULL;
    struct cixvpu_boot_report r={0}, before;
    unsigned int allocated;
    static const struct { unsigned int h, offset, value; } expected[]={
        {2,0x21c,0xa5a50ffd},{2,0x300,0x01234567},
        {1,0x50,1},{1,0x24,0},{1,0x44,2},{1,0x20c,1},
        {1,0x218,1},{1,0x200,0x1fe},{1,0x204,0},
        {1,0x210,0},{1,0x230,0x33},{1,0x234,0x22},
        {1,0x238,0x33},{1,0x23c,0x33},{1,0x21c,0},
        {1,0x220,1},{1,0x214,1},{1,4,0},{1,0x14,0xff8f7f00},{1,4,1}
    };

    boot_fixture(); allocated=allocations;
    CHECK(cixvpu_acpi_boot_create(NULL,&fixture_aa,&attach_description,&ctx)==EINVAL);
    CHECK(cixvpu_acpi_boot_create(VPU,NULL,&attach_description,&ctx)==EINVAL);
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,NULL,&ctx)==EINVAL);
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,NULL)==EINVAL);
    CHECK(ctx==NULL && allocations==allocated && maps==0 && nevents==0);
    interrupt_context=true;
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,&ctx)==EWOULDBLOCK);
    interrupt_context=false; soft_context=true;
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,&ctx)==EWOULDBLOCK);
    soft_context=false; boot_fault=1;
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,&ctx)==ENOMEM && ctx==NULL);
    boot_fault=0; ctx=boot_new(true); owned=boot_actual;
    CHECK(cixvpu_acpi_boot_create(VPU,&fixture_aa,&attach_description,&ctx)==EBUSY);
    CHECK(cixvpu_acpi_boot_once(NULL,&owned)==EINVAL && owned==boot_actual);
    CHECK(cixvpu_acpi_boot_once(ctx,NULL)==EINVAL && !ctx->report.started);
    CHECK(cixvpu_acpi_boot_report(NULL,&r)==EINVAL);
    CHECK(cixvpu_acpi_boot_report(ctx,NULL)==EINVAL);
    CHECK(cixvpu_acpi_boot_report(ctx,&ctx->report)==EINVAL);
    memset(&r,0xa5,sizeof(r)); memcpy(&before,&r,sizeof(before));
    interrupt_context=true;
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==EWOULDBLOCK && memcmp(&r,&before,sizeof(r))==0);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EWOULDBLOCK && owned==boot_actual);
    interrupt_context=false; soft_context=true;
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==EWOULDBLOCK && memcmp(&r,&before,sizeof(r))==0);
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EWOULDBLOCK && owned==boot_actual);
    soft_context=false;
    CHECK(!ctx->report.started && boot_writes==0 && boot_intrs==0 && nevents==0);
    boot_regs[1][0x14/4]=0xff8f7f6f; boot_fault=22;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==0 && owned==NULL);
    CHECK(boot_writes==sizeof(expected)/sizeof(expected[0]) && boot_barriers==boot_writes);
    for(unsigned int i=0;i<sizeof(expected)/sizeof(expected[0]);i++) {
        CHECK(boot_trace[i].h==expected[i].h && boot_trace[i].offset==expected[i].offset);
        if(i!=8) CHECK(boot_trace[i].value==expected[i].value);
        else CHECK(boot_trace[i].value!=0 && boot_fixture_exposed(boot_actual));
    }
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.moved && r.exposed && r.held);
    CHECK(r.stage==CIXVPU_BOOT_HELD && r.last_stage==CIXVPU_BOOT_RECEIVE && r.error==0);
    CHECK(r.core_attempted==15 && r.core_released==0 && r.prepared_retained);
    boot_discard_fixture(ctx);

    boot_fixture(); ctx=boot_new(true); owned=boot_actual; boot_fault=21;
    CHECK(cixvpu_acpi_boot_once(ctx,&owned)==EIO && owned==NULL);
    CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && r.exposed && r.visits==1);
    CHECK(r.prepared_retained && r.irq_masked && boot_disestablish==0);
    boot_discard_fixture(ctx);

    for(unsigned int i=0;i<7;i++) {
        boot_fixture(); ctx=boot_new(true); owned=boot_actual;
        switch(i) {
        case 0: ctx->resources.mem[0].base+=4; break;
        case 1: ctx->resources.mem[1].translated+=4; break;
        case 2: ctx->resources.mem[1].length+=4; break;
        case 3: ctx->resources.cca=1; break;
        case 4: ctx->resources.irq++; break;
        case 5: ctx->resources.irq_trigger=1; break;
        case 6: ctx->resources.nmem=1; break;
        }
        CHECK(cixvpu_acpi_boot_once(ctx,&owned)!=0 && owned==NULL);
        CHECK(cixvpu_acpi_boot_report(ctx,&r)==0 && !r.exposed && r.cleanup_error==0);
        CHECK(nevents==0 && maps==0 && boot_intrs==0 && boot_writes==0);
        boot_discard_fixture(ctx);
    }
}
static void boot_tests(void)
{
    boot_fixture();
    struct cixvpu_acpi_boot *ctx = NULL;
    CHECK(cixvpu_acpi_boot_create(VPU, &fixture_aa, &attach_description, &ctx) == 0 && ctx != NULL);
    if (ctx == NULL) return;
    boot_actual = boot_fixture_prepare();
    struct cixvpu_prepared *owned = boot_actual;
    CHECK(cixvpu_acpi_boot_once(ctx, &owned) == 0 && owned == NULL);
    struct cixvpu_boot_report report = {0};
    CHECK(cixvpu_acpi_boot_report(ctx, &report) == 0);
    CHECK(report.exposed && report.switched_in && report.pong && report.irq_masked);
    CHECK(boot_intrs == 1 && boot_unmasks == 1 && boot_masks == 1 && boot_disestablish == 0);
    CHECK(boot_regs[0][0x21c/4] == 0xa5a50ffd && boot_regs[0][0x300/4] == 0x01234567);
    CHECK(boot_regs[1][0x200/4] == 0x1fe && boot_regs[1][0x44/4] == 2);
    CHECK(boot_regs[1][0x230/4] == 0x33 && boot_regs[1][0x234/4] == 0x22);
    CHECK(boot_regs[1][0x238/4] == 0x33 && boot_regs[1][0x23c/4] == 0x33);
    CHECK(boot_regs[1][0x14/4] == 0x0f0f0f00 && boot_regs[1][1] == 1);
    CHECK(power_live && clock_live && clock_claim.claimed && main_claim.claimed);
    unsigned int writes = boot_writes;
    CHECK(cixvpu_acpi_boot_once(ctx, &owned) == EALREADY && boot_writes == writes);
    CHECK(cixvpu_prepared_free(&boot_actual) == EBUSY);
    /* Only fixture teardown after all permanent-hold assertions. */
    boot_fixture_drop(boot_actual); AcpiOsFree(ctx);
}
#endif
int main(void)
{
    int result = cixvpu_control_contract_main();
#ifdef CIXVPU_BOOT_ORCH_API
    frame_bootstrap_tests();
    frame_trial_tests();
    frame_export_tests();
    frame_advisory_tests();
    frame_fault_tests();
    boot_tests(); boot_failure_tests(); boot_matrix_tests(); boot_trace_api_tests(); boot_rpc_policy_tests(); boot_rpc_integration_tests();
#else
    CHECK(!"dedicated native boot epoch is absent");
#endif
    printf("cixvpu boot: %u checks, %u failures\n", checks, failures);
    return result || failures ? 1 : 0;
}
