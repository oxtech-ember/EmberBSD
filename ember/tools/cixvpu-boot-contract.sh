#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ -n "${CIXVPU_BOOT_EVIDENCE:-}" ]; then
    work=$CIXVPU_BOOT_EVIDENCE
    mkdir "$work"
else
    work=$(mktemp -d "${TMPDIR:-/tmp}/cixvpu-boot.XXXXXXXX")
fi
if [ -z "${CIXVPU_BOOT_EVIDENCE:-}" ]; then trap 'rm -rf "$work"' EXIT HUP INT TERM; fi
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
    SDKROOT=$(xcrun --show-sdk-path); export SDKROOT
fi
api='-DCIXVPU_CONTROL_API -DCIXVPU_ATTACH_API'
for item in control-source:sys/dev/acpi/cixvpu_acpi_control.c control-header:sys/dev/acpi/cixvpu_acpi_control.h attachment-source:sys/dev/acpi/cixvpu_acpi.c dma-header:sys/dev/ic/cixvpu_dma.h prepare-header:sys/dev/ic/cixvpu_prepare.h; do
    name=${item%%:*}; input=${item#*:}
    ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' "$src/$input" "$work/$name.inc"
done
# Import the real user-wrapper length/error transform, not a copied algorithm.
ruby - "$src/sys/kern/kern_sysctl.c" "$work/old-sysctl.inc" <<'RUBY_OLD'
s=File.read(ARGV[0]);parts=[]
%w[old_sysctl sysctl_query sysctl_cvt_in sysctl_cvt_out].each do |name|
  marker=name.start_with?('sysctl_cvt') ? "static int\n#{name}(" : "int\n#{name}("
  start=s.index(marker) or abort "#{name} absent"
  brace=s.index('{',start);depth=0;finish=nil
  (brace...s.size).each{|i|depth+=1 if s[i]=='{';depth-=1 if s[i]=='}';if depth==0;finish=i;break;end}
  abort "#{name} boundary" unless finish
  parts << s[start..finish]
end
File.write(ARGV[1],parts.reverse.join("\n")+"\n")
RUBY_OLD
# Use the fork's padded public node layout and field aliases verbatim.
ruby - "$src/sys/sys/sysctl.h" "$work/sysctl-node.inc" <<'RUBY_NODE'
s=File.read(ARGV[0]);a=s.index('struct sysctlnode {') or abort 'sysctlnode absent'
b=s.index('/*',s.index("#define\tsysctl_bdata",a)) or abort 'node aliases absent'
File.write(ARGV[1],s[a...b])
RUBY_NODE
extra=
if [ -f "$src/sys/dev/acpi/cixvpu_acpi_boot.c" ]; then
    api="$api -DCIXVPU_BOOT_ORCH_API"
    for item in boot-source:sys/dev/acpi/cixvpu_acpi_boot.c boot-header:sys/dev/acpi/cixvpu_acpi_boot.h real-prepare-header:sys/dev/ic/cixvpu_prepare.h; do
        name=${item%%:*}; input=${item#*:}
        case "$name" in
            boot-source) input=${CIXVPU_BOOT_SOURCE:-"$src/$input"};;
            *) input=$src/$input;;
        esac
        ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' "$input" "$work/$name.inc"
    done
    # The real DMA/prepare TU and its native service boundary are imported below.
    for item in dma-source:sys/dev/ic/cixvpu_dma.c prepare-source:sys/dev/ic/cixvpu_prepare.c; do
        name=${item%%:*}; input=${item#*:}
        ruby -e 'File.write(ARGV[1],File.readlines(ARGV[0]).reject { |s| s.start_with?("#include ") }.join)' "$src/$input" "$work/$name.inc"
    done
    ruby -e 'p=ARGV[0];h=File.readlines(ARGV[1]).reject { |s| s.start_with?("#include ") }.join;File.write(p,h+File.read(p))' "$work/dma-source.inc" "$src/sys/dev/ic/cixvpu_dma_private.h"
    ruby -e 's=File.read(ARGV[0]);s=s.sub("int main(void)"){"int unused_prepare_main(void)"};File.write(ARGV[1],s)' \
        "$src/ember/tools/cixvpu-prepare-contract.c" "$work/prepare-fixture.c"
    cat > "$work/prepare-bridge.c" <<'BRIDGE'
#define CIXVPU_BOOT_BRIDGE 1
#include "prepare-fixture.c"
static void (*bridge_ack_hook)(void);
static void (*bridge_rpc_commit_hook)(void);
static unsigned int bridge_acks;
static void bridge_observe(unsigned int i, bus_addr_t off, bus_size_t len, int ops) {
    boot_nevents=0; boot_observe(i,off,len,ops);
    if(i==boot_rpc && off==0 && len==4 && ops==5 &&
        word(device_memory[i],0)==2 && bridge_rpc_commit_hook!=NULL)
        bridge_rpc_commit_hook();
    if(i==boot_host && off==0 && len==2 && ops==BUS_DMASYNC_PREWRITE) {
        bridge_acks++; if(bridge_ack_hook != NULL) bridge_ack_hook();
    }
}
struct cixvpu_prepared *boot_fixture_prepare_kind(bool boot) {
    struct cixvpu_fw_plan plan; struct cixvpu_dma *owner=begin(1,&plan);
    memcpy(image+80,"r0p0-sum0005",12); memcpy(pinned_image,image,sizeof(image));
    CHECK(cixvpu_fw_plan_build(image,extent,1,&plan)==0);
    struct cixvpu_prepared *out=NULL;
    CHECK((boot ? cixvpu_prepare_boot_cpu : cixvpu_prepare_cpu)(&owner,&plan,image,extent,fixture_pin,&out)==0);
    for(unsigned int i=0;i<plan.backing_pages;i++) {
        struct cixvpu_fw_plan_page p; CHECK(cixvpu_fw_plan_page(&plan,i,&p)==0);
        if(p.role==CIXVPU_FW_PLAN_MSG_HOST) boot_host=i;
        if(p.role==CIXVPU_FW_PLAN_MSG_MVE) boot_mve=i;
        if(p.role==CIXVPU_FW_PLAN_RPC) boot_rpc=i;
    }
    sync_fixture(&plan,true); bridge_ack_hook=bridge_rpc_commit_hook=NULL; bridge_acks=0; return out;
}
struct cixvpu_prepared *boot_fixture_prepare_rpc(void) {
    struct cixvpu_fw_plan plan; struct cixvpu_dma *owner=rpc_reserve_begin(&plan);
    struct cixvpu_prepared *out=NULL;
    CHECK(cixvpu_prepare_boot_cpu(&owner,&plan,rpc_image,sizeof(rpc_image),fixture_pin,&out)==0);
    boot_host=118; boot_mve=119; boot_rpc=124;
    sync_fixture(&plan,true); bridge_ack_hook=bridge_rpc_commit_hook=NULL; bridge_acks=0;
    return out;
}
void boot_fixture_set_rpc_commit(void (*hook)(void)) { bridge_rpc_commit_hook=hook; }
void boot_fixture_rpc_words(uint32_t state,uint32_t call,uint32_t size,
    uint32_t initial,uint32_t max,unsigned int region,unsigned int align) {
    CHECK(region<=255 && align<=255);
    uint8_t *p=device_memory[boot_rpc];
    put32(p,state); put32(p+4,call); put32(p+8,size);
    put32(p+12,initial); put32(p+16,max); p[20]=(uint8_t)region; p[21]=(uint8_t)align;
    p[22]=0xc3; p[23]=0xd4;
}
uint32_t boot_fixture_rpc_word(unsigned int index) { CHECK(index<6); return word(device_memory[boot_rpc],index); }
struct cixvpu_prepared *boot_fixture_prepare(void) { return boot_fixture_prepare_kind(true); }
void boot_fixture_set_ack(void (*hook)(void)) { bridge_ack_hook=hook; }
unsigned int boot_fixture_acks(void) { return bridge_acks; }
void boot_fixture_runtime(bool messages) { boot_nevents=0; sync_observer=bridge_observe; if(messages) { boot_message(0,2001,4,0,0x407d1); boot_message(2,2009,0,0,0x40faa); } }
bool boot_fixture_exposed(struct cixvpu_prepared *p) { return p->dma->ever_visible; }
void boot_fixture_rpc(uint32_t state, uint32_t callid, uint32_t size) { put32(device_memory[boot_rpc],state); put32(device_memory[boot_rpc]+4,callid); put32(device_memory[boot_rpc]+8,size); }
void boot_fixture_rpc_alloc(void) { boot_fixture_rpc(1,2,12); put32(device_memory[boot_rpc]+12,0x12345678); put32(device_memory[boot_rpc]+16,0xfedcba98); device_memory[boot_rpc][20]=1; device_memory[boot_rpc][21]=31; device_memory[boot_rpc][22]=0xc3; device_memory[boot_rpc][23]=0xd4; }
void boot_fixture_drop(struct cixvpu_prepared *p) { boot_drop(p); }
unsigned int boot_fixture_live(void) { return depth + live_kmem; }
void boot_fixture_map_valid(struct cixvpu_prepared *p, bool valid) { p->dma->owners[p->dma->count-1].map->dm_nsegs=valid ? 1 : 0; }
static unsigned int frame_wire_step, frame_wire_rpc, frame_wire_scenario;
static unsigned int frame_wire_debug,frame_wire_idle,frame_wire_ticks;
static bool frame_wire_waiting;
static uint8_t frame_wire_delayed[40],frame_wire_input[72];
static bool frame_wire_confirm_pending;
static uint16_t frame_wire_msg, frame_wire_buffers[2];
static uint32_t frame_wire_sum;
static uint8_t frame_wire_pixels[24576];

struct cixvpu_prepared *boot_fixture_prepare_frame(void) {
    struct cixvpu_fw_plan plan;struct cixvpu_frame_source old;
    struct cixvpu_dma *raw=frame_slots_begin(&plan,&old);
    CHECK(cixvpu_dma_free(&raw)==0);reset();seal_observer=NULL;
    const struct cixvpu_fw_slot_request request={128,128,128,128,65536};
    CHECK(cixvpu_fw_plan_build_frame_slots(rpc_image,sizeof(rpc_image),1,&request,&plan)==0);
    CHECK(plan.backing_pages==414 && plan.mappings==407);
    CHECK(cixvpu_dma_alloc(&tag32,&tag64,&plan,&raw)==0);
    for(unsigned int y=0;y<128;y++)for(unsigned int x=0;x<128;x++)
        frame_wire_pixels[128*y+x]=(uint8_t)(16+28*((x/16+2*(y/16))%8));
    for(unsigned int i=16384;i<24576;i+=2) {frame_wire_pixels[i]=90;frame_wire_pixels[i+1]=160;}
    const struct cixvpu_frame_source source={frame_wire_pixels,frame_wire_pixels+16384,16384,8192};
    struct cixvpu_prepared *p=NULL;
    CHECK(cixvpu_prepare_frame_slots_cpu(&raw,&plan,rpc_image,sizeof(rpc_image),fixture_pin,&source,&p)==0);
    boot_host=118;boot_mve=119;boot_rpc=124;sync_fixture(&plan,true);
    bridge_ack_hook=bridge_rpc_commit_hook=NULL;bridge_acks=0;
    frame_wire_step=frame_wire_rpc=frame_wire_debug=frame_wire_idle=frame_wire_ticks=0;frame_wire_waiting=frame_wire_confirm_pending=false;frame_wire_buffers[0]=frame_wire_buffers[1]=0;
    return p;
}
static void frame_wire_message(uint16_t code,const uint8_t *data,size_t n) {
    uint8_t *m=device_memory[119];uint32_t header=code|(uint32_t)n<<16;
    put32(m+16+4*frame_wire_msg,header);frame_wire_sum+=header;
    for(size_t i=0;i<n;i++) {
        m[16+(4*(frame_wire_msg+1)+i)%4080]=data[i];
        frame_wire_sum+=(uint32_t)data[i]<<((i%4)*8);
    }
    frame_wire_msg=(uint16_t)((frame_wire_msg+1+(n+3)/4)%1020);
    m[0]=(uint8_t)frame_wire_msg;m[1]=(uint8_t)(frame_wire_msg>>8);
    put32(m+4,word(m,2));put32(m+8,word(m,3));put32(m+12,frame_wire_sum);
}
void boot_fixture_frame_start(unsigned int scenario) {
    frame_wire_scenario=scenario;boot_fixture_runtime(false);
    boot_fixture_rpc_words(1,2,12,131072,67108864,0,22);
    device_memory[119][2]=2;device_memory[119][3]=0;
}
static void frame_wire_buffer(unsigned int q,uint16_t code,const uint8_t *data,size_t n) {
    uint8_t *m=device_memory[q==1?121:123];uint16_t pos=frame_wire_buffers[q-1];
    put32(m+16+4*pos,code|(uint32_t)n<<16);
    for(size_t i=0;i<n;i++)m[16+(4*(pos+1)+i)%4080]=data[i];
    pos=(uint16_t)((pos+1+(n+3)/4)%1020);m[0]=(uint8_t)pos;m[1]=(uint8_t)(pos>>8);
    frame_wire_buffers[q-1]=pos;
}
void boot_fixture_frame_notify(void) {
    if(frame_wire_rpc<3) {
        CHECK(boot_fixture_rpc_word(0)==2);frame_wire_rpc++;
        if(frame_wire_rpc==1)boot_fixture_rpc_resize(0x20000000,524288);
        else if(frame_wire_rpc==2)boot_fixture_rpc_words(1,2,12,16384,16384,0,12);
        else {boot_fixture_rpc(0,0,0);boot_fixture_runtime(true);frame_wire_msg=3;frame_wire_sum=0x40faa;}
        return;
    }
    if(frame_wire_rpc==3 && boot_fixture_rpc_word(0)==2 && boot_fixture_rpc_word(1)==1) {
        CHECK(frame_wire_debug<16);frame_wire_debug++;
        if(frame_wire_debug<16)boot_fixture_rpc_words(1,1,1012,0,0,0,0);
        else boot_fixture_rpc(0,0,0);
        return;
    }
    /* Each received IDLE permits exactly one corresponding publication. */
    uint16_t early_read=(uint16_t)device_memory[119][2]|(uint16_t)device_memory[119][3]<<8;
    if((word(device_memory[118],4+early_read)&65535)==1012) {
        uint8_t *host=device_memory[118],*mve=device_memory[119];
        CHECK(frame_wire_scenario>=20 && frame_wire_scenario<=22 && frame_wire_idle<3);
        CHECK(word(host,4+early_read)==1012);
        mve[2]=host[2];mve[3]=host[3];frame_wire_idle++;
        if(frame_wire_idle<3)frame_wire_message(2013,NULL,0);
        return;
    }
    static const struct {unsigned int q,code,n,a,b;} expected[]={
        {0,1009,8,0x10001,0},{0,1010,8,1,1},{0,1010,8,3,0xd0001},
        {0,1010,8,16,0},{0,1010,8,17,0},{0,1010,8,56,26},
        {0,1001,0,0,0},{0,1006,0,0,0},{1,3003,8,9,0x1e0000},
        {1,3003,16,10,0},{1,3001,72,1,0},{2,3002,40,2,0},{2,3002,40,2,0}
    };
    CHECK(frame_wire_step<15);if(frame_wire_step>=15)return;
    unsigned int step=frame_wire_step++,literal=step<13?step:12,q=expected[literal].q,pending=0;
    uint16_t read=0,write=0;
    for(unsigned int j=0;j<3;j++) {
        unsigned int h=118+2*j;uint8_t *host=device_memory[h],*mve=device_memory[h+1];
        uint16_t r=(uint16_t)mve[2]|(uint16_t)mve[3]<<8;
        uint16_t w=(uint16_t)host[2]|(uint16_t)host[3]<<8;
        if(r!=w) {pending++;CHECK(j==q);read=r;write=w;}
    }
    CHECK(pending==1);
    uint8_t *host=device_memory[118+2*q],*mve=device_memory[119+2*q];
    uint32_t header=word(host,4+read);unsigned int code=header&65535,n=header>>16;
    CHECK(code==expected[literal].code && n==expected[literal].n);
    uint8_t payload[72]={0};
    for(unsigned int i=0;i<n && i<72;i++)payload[i]=host[16+(4*(read+2)+i)%4080];
    if(n)CHECK(word(payload,0)==expected[literal].a && word(payload,1)==expected[literal].b);
    CHECK(write==(read+2+(n+3)/4)%1020);
    mve[2]=(uint8_t)write;mve[3]=(uint8_t)(write>>8);
    if(step>=1 && step<=5) {
        uint8_t confirmed[132]={0};put32(confirmed,expected[literal].a);
        if(step==1 && frame_wire_scenario==2)put32(confirmed,99);
        if(step==1 && (frame_wire_scenario==3 || frame_wire_scenario==30))return;
        if(step==1 && frame_wire_scenario==28) {frame_wire_confirm_pending=true;return;}
        if(step==1 && (frame_wire_scenario==4 || frame_wire_scenario==5)) {
            frame_wire_message(frame_wire_scenario==4?2010:2017,confirmed,132);return;
        }
        frame_wire_message(2003,confirmed,frame_wire_scenario==1?0:4);
    } else if(step==6) {
        if(frame_wire_scenario==6)boot_fixture_rpc_words(1,4,12,16384,16384,0,12);
        if(frame_wire_scenario>=20 && frame_wire_scenario<=22)boot_fixture_rpc_words(1,1,1012,0,0,0,0);
        if(frame_wire_scenario==23)for(unsigned int j=0;j<130;j++)frame_wire_message(2009,NULL,0);
    } else if(step==7) {
        frame_wire_message(2009,NULL,0);
        if(frame_wire_scenario>=20 && frame_wire_scenario<=22)frame_wire_message(2013,NULL,0);
    }
    else if(step==10) {
        CHECK(word(payload,2)==3 && word(payload,3)==0 && word(payload,4)==0x10404);
        CHECK(word(payload,5)==0x00800080 && word(payload,6)==0x201);
        if(frame_wire_scenario==7)put32(payload+8,4);
        if(frame_wire_scenario==8)put32(payload+16,word(payload,4)|0x1000);
        if(frame_wire_scenario==9)put32(payload+16,word(payload,4)|0x2000);
        if(frame_wire_scenario==27)memcpy(frame_wire_input,payload,72);
        else {frame_wire_buffer(1,3001,payload,72);frame_wire_message(2005,NULL,0);}

    } else if(step>=11) {
        CHECK(word(payload,2)==3 && word(payload,5)==65536 && word(payload,8)==0x24400000);
        put32(payload+16,step==11?0x80:0x10);put32(payload+24,step==11?3:4095);put32(payload+28,step==11?3:5);
        if(step==11) {device_memory[391][3]=0;device_memory[391][4]=0;device_memory[391][5]=1;}
        else {device_memory[391][4095]=0x26;for(unsigned int i=0;i<4;i++)device_memory[392][i]=(uint8_t)(i+1);}
        if(frame_wire_scenario==10)put32(payload+8,4);
        if(frame_wire_scenario==11)put32(payload+16,0x200);
        if(frame_wire_scenario==29)put32(payload+28,65537);
        if(frame_wire_scenario==12) {put32(payload+16,0x10);put32(payload+28,0);}
        if(frame_wire_scenario==24 || frame_wire_scenario==25) {
            put32(payload+16,frame_wire_scenario==24 && step==14?0x10:0x80);
            put32(payload+24,0);put32(payload+28,65536);
            for(unsigned int j=0;j<16;j++)memset(device_memory[391+j],0x31+step,4096);
        }
        if((frame_wire_scenario>=20 && frame_wire_scenario<=22) && step==12) {
            memcpy(frame_wire_delayed,payload,40);frame_wire_waiting=true;
            if(frame_wire_scenario==21)boot_fixture_rpc_words(1,1,1012,0,0,0,0);
        } else {frame_wire_buffer(2,3002,payload,40);frame_wire_message(2006,NULL,0);}
        if(frame_wire_scenario==27 && step==12) {frame_wire_buffer(1,3001,frame_wire_input,72);frame_wire_message(2005,NULL,0);}

    }
}
void boot_fixture_frame_tick(void) {
    if(frame_wire_confirm_pending && ++frame_wire_ticks==10) {
        uint8_t confirm[4];put32(confirm,1);frame_wire_confirm_pending=false;frame_wire_message(2003,confirm,4);
    }
    if(frame_wire_waiting && ++frame_wire_ticks==10) {

        frame_wire_waiting=false;frame_wire_buffer(2,3002,frame_wire_delayed,40);frame_wire_message(2006,NULL,0);
    }
}
unsigned int boot_fixture_frame_publications(void) {return frame_wire_step;}

BRIDGE
    extra="$work/prepare-bridge.c $src/sys/dev/ic/cixvpu_mmu.c $src/sys/dev/ic/cixvpu_ring.c $src/sys/dev/ic/cixvpu_command.c $src/sys/dev/ic/cixvpu_buffer.c"
    api="$api -DCIXVPU_DMA_API -DCIXVPU_SYNC_API -DCIXVPU_PREPARE_API -DCIXVPU_BOOT_API"
fi
if ruby -e 'exit(File.read(ARGV[0]).include?("sc_boot_once_invoked") ? 0 : 1)' \
    "$src/sys/dev/acpi/cixvpu_acpi.c"; then api="$api -DCIXVPU_BOOT_ATTACH_API"; fi
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} $api \
    -isystem "$src/sys/external/bsd/acpica/dist/include" -I"$src/sys/dev/ic" -I"$src/ember/tools" -I"$work" \
    "$src/ember/tools/cixvpu-boot-contract.c" "$src/sys/dev/ic/cixvpu_resources.c" \
    "$src/sys/dev/ic/cixvpu_fw.c" "$src/sys/dev/ic/cixvpu_fw_plan.c" $extra -o "$work/test"
if [ "${CIXVPU_BOOT_ANALYZE:-0}" = 1 ]; then
    ${CC:-cc} -std=c11 -dM -E -x c /dev/null > "$work/predefined.h"
    set --
    if [ "$(uname -s)" = Darwin ]; then set -- -isysroot "$SDKROOT"; fi
    for input in "$src/ember/tools/cixvpu-boot-contract.c" "$work/prepare-bridge.c"; do
name=$(basename "$input" .c)
# Keep each native-service namespace in its actual translation unit.
cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
    --error-exitcode=1 --include="$work/predefined.h" --std=c11 --platform=unix64 \
    -DACPI_APPLICATION $api -I"$work" -I"$src/sys/dev/ic" -I"$src/ember/tools" \
    -I"$src/sys/external/bsd/acpica/dist/include" "$input"
        "${CSA:-clang}" --analyze -Xanalyzer -analyzer-output=text -std=c11 -Wall -Wextra -Werror \
            -DACPI_APPLICATION $api -isystem "$src/sys/external/bsd/acpica/dist/include" \
            -I"$work" -I"$src/sys/dev/ic" -I"$src/ember/tools" "$@" "$input" \
            -o "$work/$name.plist" 2> "$work/$name.csa.log"
        cat "$work/$name.csa.log"
        # Keep the unchanged native query's existing dead initialization visible.
        # Classify only that exact imported line. New diagnostics still fail.
        ruby - "$work/$name.csa.log" "$work/old-sysctl.inc" <<'RUBY_CSA'
log=File.read(ARGV[0]);source=File.readlines(ARGV[1])
start=source.index{|line|line.start_with?('sysctl_query(')} or abort 'native query absent'
line=(start...source.size).find{|i|source[i].strip=='error = 0;'} or abort 'native query initialization absent'
expected="#{ARGV[1]}:#{line+1}:2: warning: Value stored to 'error' is never read [deadcode.DeadStores]"
warnings=log.lines.select{|entry|entry.include?(': warning:')}.map(&:strip)
abort 'new CSA diagnostic' unless warnings.empty? || warnings==[expected]
RUBY_CSA

    done
fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
