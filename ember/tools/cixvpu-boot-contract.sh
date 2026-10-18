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
BRIDGE
    extra="$work/prepare-bridge.c $src/sys/dev/ic/cixvpu_mmu.c $src/sys/dev/ic/cixvpu_ring.c"
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
        test ! -s "$work/$name.csa.log"
    done
fi
if [ -n "${TEST_RUNNER:-}" ]; then "$TEST_RUNNER" "$work/test"; else "$work/test"; fi
