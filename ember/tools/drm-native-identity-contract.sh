#!/bin/sh
# Origin: EmberBSD; AI-assisted production DRM identity and sync-fd contracts.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/drm-identity.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
extract() {
    awk -v name="$1" -v type="$3" '
        $0 ~ "^((static )?(void|int) )?" name "[(]" && $0 !~ /;[[:space:]]*$/ {
            print type; sub(/^(static )?(void|int) /, ""); copying = 1;
        }
        copying { print }
        copying && /^}/ { exit }
    ' "$2"
}
cp "$src/ember/tools/drm-native-identity-fixture.c" "$work/test.c"
sysctl="$src/sys/external/bsd/drm2/drm/drm_sysctl.c"
# Include the real owned storage declaration and production functions verbatim.
sed -n '/^struct drm_native_identity {/,/^};/p' "$sysctl" >> "$work/test.c"
extract drm_sysctl_identity_register "$sysctl" int >> "$work/test.c"
extract drm_sysctl_identity_unregister "$sysctl" void >> "$work/test.c"
extract drm_dev_register "$src/sys/external/bsd/drm2/dist/drm/drm_drv.c" int >> "$work/test.c"
extract drm_dev_unregister "$src/sys/external/bsd/drm2/dist/drm/drm_drv.c" void >> "$work/test.c"
extract sync_file_get_fence "$src/sys/external/bsd/drm2/linux/linux_sync_file.c" 'struct dma_fence *' >> "$work/test.c"
cat >> "$work/test.c" <<'C'
int main(void)
{
    struct drm_minor primary = { .index = 3 }, render = { .index = 140 };
    struct drm_driver driver = { .load = load, .unload = unload };
    struct drm_device dev = { .primary = &primary, .render = &render, .driver = &driver };
    struct drm_native_pci_record saved;
    pthread_t thread;
    assert(sizeof(saved) == 68);
    dev.native_pci = (struct drm_native_pci_record) {
        .version=1, .length=68, .flags=DRM_NATIVE_REVISION, .bus_type=1,
        .domain=2, .bus=5, .device=7, .function=1, .vendor=0x1af4,
        .product=0x1050, .subvendor=0x1af4, .subproduct=0x1100, .revision=2
    };
    /* A failed driver load never publishes identity. */
    load_error=-EIO;
    assert(drm_dev_register(&dev, 0)==-EIO);
    assert(!dev.registered && !dev.native_identity && !active[3] && !active[140]);
    load_error=0;
    /* Parent and both leaf failure positions unwind completely. */
    for (fail_at=1; fail_at<=3; fail_at++) {
        create_count=0;
        assert(drm_dev_register(&dev, 0)==-ENOMEM);
        assert(!dev.registered && !dev.native_identity && !leaves[3] && !leaves[140]);
        assert(!active[3] && !active[140]);
    }
    fail_at=0;
    assert(drm_dev_register(&dev, 0)==0);
    assert(dev.registered && leaves[3] && leaves[140]);
    saved=*leaves[140];
    assert(saved.primary_minor==3 && saved.render_minor==140);
    assert(saved.primary_major==180 && saved.render_major==180);
    assert(saved.vendor==0x1af4 && saved.subproduct==0x1100 && saved.domain==2);
    assert(saved.flags==7);
    /* Cached metadata is immutable even if the source cache is changed. */
    dev.native_pci.product=0x9999;
    assert(leaves[140]->product==0x1050);
    /* Simulate a sysctl reader holding the native tree read lock. */
    assert(pthread_rwlock_rdlock(&tree)==0);
    teardown_waiting=false;
    assert(pthread_create(&thread, NULL, unregister_thread, &dev)==0);
    assert(pthread_mutex_lock(&gate)==0);
    while (!teardown_waiting) assert(pthread_cond_wait(&ready, &gate)==0);
    assert(leaves[140]->product==saved.product && allocated==1);
    assert(pthread_mutex_unlock(&gate)==0);
    assert(pthread_rwlock_unlock(&tree)==0);
    assert(pthread_join(thread, NULL)==0);
    assert(!leaves[3] && !leaves[140] && !dev.native_identity && allocated==0);
    /* Reuse publishes a fresh record, with no old render membership. */
    dev.render=NULL;
    assert(drm_dev_register(&dev, 0)==0);
    assert(leaves[3]->product==0x9999 && !(leaves[3]->flags & DRM_NATIVE_RENDER));
    assert(!leaves[140]);
    drm_dev_unregister(&dev);
    /* Drivers with no metadata preserve their registration behavior. */
    dev.native_pci.version=0;
    assert(drm_dev_register(&dev, 0)==0 && !dev.native_identity);
    drm_dev_unregister(&dev);
/* Platform identity has the same ownership, but no fabricated PCI IDs. */
dev.render=&render;
dev.native_platform=(struct drm_native_platform_record) {
    .version=1, .length=160, .bus_type=DRM_NATIVE_BUS_PLATFORM,
    .fullname="bcmv3d0", .compatible="acpi:BCM2850"
};
for (fail_at=1; fail_at<=3; fail_at++) {
    create_count=0;
    assert(drm_dev_register(&dev,0)==-ENOMEM);
    assert(!dev.native_identity && !platform_leaves[3] && !platform_leaves[140]);
    assert(!active[3] && !active[140]);
}
fail_at=0;
assert(drm_dev_register(&dev,0)==0);
assert(!leaves[3] && !leaves[140]);
assert(platform_leaves[3] && platform_leaves[140]);
struct drm_native_platform_record platform_saved=*platform_leaves[140];
assert(platform_saved.primary_minor==3 && platform_saved.render_minor==140);
assert(platform_saved.primary_major==180 && platform_saved.render_major==180);
assert(platform_saved.flags==3 && platform_saved.length==160);
dev.native_platform.fullname[0]='x';
assert(!strcmp(platform_leaves[140]->fullname,"bcmv3d0"));
assert(pthread_rwlock_rdlock(&tree)==0);
teardown_waiting=false;
assert(pthread_create(&thread,NULL,unregister_thread,&dev)==0);
assert(pthread_mutex_lock(&gate)==0);
while (!teardown_waiting) assert(pthread_cond_wait(&ready,&gate)==0);
assert(!memcmp(platform_leaves[140],&platform_saved,sizeof(platform_saved)));
assert(pthread_mutex_unlock(&gate)==0);
assert(pthread_rwlock_unlock(&tree)==0);
assert(pthread_join(thread,NULL)==0);
assert(!platform_leaves[3] && !platform_leaves[140] && allocated==0);
dev.render=NULL;
assert(drm_dev_register(&dev,0)==0);
assert(platform_leaves[3]->flags==DRM_NATIVE_PRIMARY);
assert(!platform_leaves[140]);
drm_dev_unregister(&dev);
    /* File identity is checked before any foreign f_data dereference. */
    assert(sync_file_get_fence(-1)==NULL && held==0);
    for (int type=1; type<=3; type++) {
        files[0]=(struct file){ .f_type=type, .f_ops=&foreign_ops, .f_data=(void *)1 };
        assert(sync_file_get_fence(0)==NULL && held==0);
    }
    files[0]=(struct file){ .f_type=1, .f_ops=&sync_file_ops, .f_data=(void *)1 };
    assert(sync_file_get_fence(0)==NULL);
    files[0]=(struct file){ .f_type=DTYPE_MISC, .f_ops=&sync_file_ops };
    assert(sync_file_get_fence(0)==NULL);
    struct dma_fence fence={0}; struct sync_file sf={&fence};
    files[0].f_data=&sf;
    assert(sync_file_get_fence(0)==&fence && fence.refs==1 && held==0);
    puts("PASS: production registration/unwind, immutable identity, reader drain, minor reuse and sync-fd identity");
    return 0;
}
C
${CC:-cc} -std=c11 -D_XOPEN_SOURCE=700 -Wall -Wextra -Werror -pthread \
    -I"$src/sys/external/bsd/drm2/include/drm" "$work/test.c" -o "$work/test"
"$work/test"
# The actual PCI locator/snapshot function, with only autoconf/config access mocked.
cat > "$work/pci.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "drm_native_identity.h"
#define PCICF_DEV 0
#define PCICF_FUNCTION 1
#define PCI_ID_REG 0
#define PCI_CLASS_REG 8
#define PCI_SUBSYS_ID_REG 44
#define PCI_VENDOR(v) ((v)&0xffff)
#define PCI_PRODUCT(v) (((v)>>16)&0xffff)
#define PCI_SUBSYS_VENDOR PCI_VENDOR
#define PCI_SUBSYS_ID PCI_PRODUCT
#define PCI_REVISION(v) ((v)&255)
typedef int pcitag_t;
typedef uint32_t pcireg_t;
struct pci_softc { int sc_pc, sc_bus; };
typedef struct device { struct device *parent; struct pci_softc *private; bool pci; } *device_t;
struct drm_device { device_t dev; struct drm_native_pci_record native_pci; };
static struct pci_softc softc={4,5};
static struct device bus={NULL,&softc,true}, transport={&bus,NULL,false}, gpu={&transport,NULL,false};
static int slot=7, function=2, reads;
static uint32_t id=0x10501af4;
static char unique[64];
static device_t device_parent(device_t d) { return d->parent; }
static bool device_is_a(device_t d,const char *s) { assert(!strcmp(s,"pci"));return d->pci; }
static void *device_private(device_t d) { return d->private; }
static const char *device_xname(device_t d) { assert(d==&gpu);return "virtiodrm0"; }
static int device_locator(device_t d,int loc) { assert(d==&transport); return loc==PCICF_DEV ? slot : function; }
static int pci_get_segment(int pc) { assert(pc==4);return 9; }
static int pci_make_tag(int pc,int b,int d,int f) { assert(pc==4 && b==5 && d==7 && f==2);return 123; }
static uint32_t pci_conf_read(int pc,int tag,int reg) {
    assert(pc==4 && tag==123); reads++;
    switch(reg) { case PCI_ID_REG:return id;case PCI_CLASS_REG:return 0x03000009;
    case PCI_SUBSYS_ID_REG:return 0x1234abcd; default:assert(0);return 0; }
}
static int drm_dev_set_unique(struct drm_device *d,const char *name) { (void)d;snprintf(unique,sizeof(unique),"%s",name);return 0; }
#define aprint_normal_dev(...) ((void)0)
C
extract virtiodrm_set_busid "$src/sys/external/bsd/drm2/virtio/virtgpu_autoconf.c" 'static int' >> "$work/pci.c"
cat >> "$work/pci.c" <<'C'
int main(void) {
    struct drm_device d={.dev=&gpu};
    assert(virtiodrm_set_busid(&d)==0 && reads==3);
    assert(!strcmp(unique,"pci:0009:05:07.2"));
    assert(d.native_pci.domain==9 && d.native_pci.bus==5 && d.native_pci.device==7);
    assert(d.native_pci.function==2 && d.native_pci.vendor==0x1af4);
    assert(d.native_pci.product==0x1050 && d.native_pci.subvendor==0xabcd);
    assert(d.native_pci.subproduct==0x1234 && d.native_pci.revision==9);
    assert(d.native_pci.flags==DRM_NATIVE_REVISION);
    slot=32; assert(virtiodrm_set_busid(&d)==-ENODEV && reads==3);slot=7;
    function=-1;assert(virtiodrm_set_busid(&d)==-ENODEV && reads==3);function=2;
    id=0xffffffff;assert(virtiodrm_set_busid(&d)==-ENODEV);
    memset(&d.native_pci,0,sizeof(d.native_pci));bus.pci=false;
    assert(virtiodrm_set_busid(&d)==0 && d.native_pci.version==0);
    assert(!strcmp(unique,"virtiodrm0"));
    puts("PASS: production PCI snapshot uses real parent, segment, locators and IDs; invalid/absent PCI rejected");
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    -I"$src/sys/external/bsd/drm2/include/drm" "$work/pci.c" -o "$work/pci"
"$work/pci"
