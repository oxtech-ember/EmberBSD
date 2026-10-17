#!/bin/sh
# Origin: EmberBSD production native console regressions, 2026-10-06.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-console.XXXXXXXX")
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
console="$src/sys/external/bsd/drm2/virtio/virtgpu_console.c"
cat > "$work/console.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#undef putchar
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include "virtgpu_limits.h"
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define READ_ONCE(x) (x)
#define membar_producer() ((void)0)
#define membar_consumer() ((void)0)
#define msecs_to_jiffies(x) (x)
#define WSDISPLAYIO_MODE_EMUL 0
struct mutex { bool held; };
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held=true; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held=false; }
#define spin_lock mutex_lock
#define spin_unlock mutex_unlock
static unsigned atomic_swap_uint(volatile unsigned *p, unsigned x) {
    unsigned old=*p; *p=x; return old;
}
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
struct drm_framebuffer { void *obj[1]; };
struct drm_device { void *dev_private; };
struct drm_file { int unused; };
struct drm_fb_helper { struct drm_framebuffer *fb; };
struct drmfb_softc { struct { struct drm_fb_helper *da_fb_helper; } sc_da; };
struct virtgpu_console;
struct virtio_gpu_device {
    struct { void *dmat; } *vdev;
    bool console_takeover;
    struct virtgpu_console *console; bool vqs_ready; int submit_error;
};
struct virtgpu_console {
    struct drm_fb_helper helper;
    struct virtio_gpu_device *vgdev;
    struct mutex lock, schedule_lock;
    void *wq;
    struct delayed_work work;
    void *shadow;
    size_t size;
    volatile unsigned dirty;
    bool emul, master, restore, stopped; int error;
};
struct virtio_gpu_object {
    void *dma_vaddr; unsigned width, height, hw_res_handle;
    struct { struct { size_t size; } base; } base;
    struct { struct { void *sg_dmamap; } *sgl; } *pages;
};
#define gem_to_virtio_gpu_obj(x) ((struct virtio_gpu_object *)(x))
static struct virtgpu_console *current;
static unsigned restores, transfers, flushes, queued, stops, cancels;
static unsigned scanouts, retry_delay;
static bool inject_damage, fail_alloc;
static int transfer_error;
static void virtgpu_console_stop(struct virtio_gpu_device *);
static void virtio_gpu_stop(struct virtio_gpu_device *v, int error) {
    assert(!v->console->lock.held); stops++; v->submit_error=error;
    v->vqs_ready=false; virtgpu_console_stop(v);
}
static int drm_fb_helper_restore_fbdev_mode_unlocked(struct drm_fb_helper *h) {
    assert(h == &current->helper && current->lock.held);
    restores++; return 0;
}
static void queue_delayed_work(void *q, struct delayed_work *w, int delay) {
    (void)q; (void)w; retry_delay=delay;
    assert(current->schedule_lock.held && !current->stopped); queued++;
}
static void cancel_delayed_work_sync(struct delayed_work *w) {
    assert(w == &current->work && current->stopped); cancels++;
}
/* The real controlled DMA helper is extracted in controlled-2d-contract.sh. */
static int virtio_gpu_console_copy_upload(struct virtio_gpu_device *v,
    struct virtio_gpu_object *bo, const void *shadow, size_t bytes) {
    assert(v == current->vgdev && current->lock.held);
    assert(!current->master && current->emul && bo==current->helper.fb->obj[0]);
    assert(bytes==current->size && shadow==current->shadow);
    if(fail_alloc) return -ENOMEM;
    memcpy(bo->dma_vaddr,shadow,bytes);
    transfers++;
    if (inject_damage) {
        unsigned char before=((unsigned char *)bo->dma_vaddr)[0];
        memset(current->shadow, 0x7b, current->size);
        atomic_swap_uint(&current->dirty, 1);
        assert(((unsigned char *)bo->dma_vaddr)[0] == before);
        inject_damage=false;
    }
    return transfer_error;
}
static int virtio_gpu_cmd_set_scanout(struct virtio_gpu_device *v, unsigned index,
    unsigned id, unsigned w, unsigned h, unsigned x, unsigned y) {
    assert(v == current->vgdev && index==0 && id==1 && w==32 && h==32);
    assert(!x && !y); scanouts++; return 0;
}
static int virtio_gpu_cmd_resource_flush(struct virtio_gpu_device *v,
    unsigned id, unsigned x, unsigned y, unsigned w, unsigned h) {
    assert(v == current->vgdev && id == 1 && !x && !y && w==32 && h==32);
    flushes++; return 0;
}
C
for pair in 'virtgpu_console_geometry size_t' 'virtgpu_console_damage void' \
 'virtgpu_console_mode bool' 'virtgpu_console_upload int' \
 'virtgpu_console_work void' 'virtgpu_console_master_set int' \
 'virtgpu_console_master_drop void' 'virtgpu_console_stop void' \
 'virtgpu_console_drain void'; do
    set -- $pair
    extract "$1" "$console" "static $2" >> "$work/console.c"
done
cat >> "$work/console.c" <<'C'
int main(void) {
    unsigned char shadow[4096], dma[4096];
    struct virtio_gpu_object bo={ .dma_vaddr=dma, .width=32, .height=32,
        .hw_res_handle=1 };
    struct drm_framebuffer fb={ .obj={ &bo } };
    __typeof__(*bo.pages) pages; __typeof__(*pages.sgl) sg;
    __typeof__(* ((struct virtio_gpu_device *)0)->vdev) vd;
    memset(&vd,0,sizeof(vd)); memset(&sg,0,sizeof(sg));
    pages.sgl=&sg; bo.pages=&pages;
    struct virtio_gpu_device v={ .vqs_ready=true };
    struct virtgpu_console vc={ .helper={ &fb }, .vgdev=&v,
        .shadow=shadow, .size=sizeof(shadow), .emul=true };
    struct drm_device dev={ &v };
    struct drmfb_softc sc={ .sc_da={ &vc.helper } };
    current=&vc; v.console=&vc; v.vdev=&vd;
    assert(!virtgpu_console_geometry(0,32));
    assert(!virtgpu_console_geometry(32,31));
    assert(!virtgpu_console_geometry(UINT32_MAX,8192));
    assert(!virtgpu_console_geometry(16384,32));
    assert(!virtgpu_console_geometry(32,8193));
    assert(virtgpu_console_geometry(800,600)==1920000);
    memset(shadow,0x11,sizeof(shadow));
    virtgpu_console_damage(&sc); inject_damage=true;
    virtgpu_console_work(&vc.work.work);
    assert(transfers==1 && flushes==1 && vc.dirty==1 && dma[0]==0x11);
    virtgpu_console_work(&vc.work.work);
    assert(transfers==2 && flushes==2 && vc.dirty==0 && dma[0]==0x7b);
    virtgpu_console_work(&vc.work.work); assert(transfers==2);
    assert(!virtgpu_console_master_set(&dev,NULL,true));
    virtgpu_console_damage(&sc); vc.restore=true;
    virtgpu_console_work(&vc.work.work);
    assert(restores==0 && transfers==2 && vc.dirty);
    virtgpu_console_mode(&sc,1); virtgpu_console_master_drop(&dev,NULL);
    virtgpu_console_work(&vc.work.work); assert(restores==0 && transfers==2);
    virtgpu_console_mode(&sc,0); virtgpu_console_work(&vc.work.work);
    assert(restores==1 && transfers==3 && flushes==3);
    virtgpu_console_damage(&sc); transfer_error=-EIO;
    unsigned q=queued; virtgpu_console_work(&vc.work.work);
    assert(stops==1 && vc.stopped && !v.vqs_ready && queued==q);
    virtgpu_console_damage(&sc); virtgpu_console_work(&vc.work.work);
    assert(transfers==4 && queued==q);
    virtgpu_console_drain(&v); assert(cancels==1);
    v.vqs_ready=true; v.submit_error=0; vc.stopped=false;
    transfer_error=0; fail_alloc=true;
    virtgpu_console_damage(&sc); virtgpu_console_work(&vc.work.work);
    assert(stops==1 && !vc.stopped && transfers==4 && flushes==3);
    assert(vc.error==-ENOMEM && vc.restore && vc.dirty && retry_delay==1000);
    assert(virtgpu_console_master_set(&dev,NULL,true)==-ENOMEM);
    fail_alloc=false; virtgpu_console_work(&vc.work.work);
    assert(!vc.error && !vc.restore && transfers==5 && flushes==4);
    assert(scanouts==2 && retry_delay==20);
    assert(!virtgpu_console_master_set(&dev,NULL,true));
    puts("VirtGPU production console geometry, damage/upload, ownership and stop contracts passed");
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter \
 -I"$src/sys/external/bsd/drm2/virtio" "$work/console.c" -o "$work/console"
"$work/console"
cat > "$work/selection.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#undef putchar
#define KASSERT assert
#define KASSERTMSG(condition, ...) assert(condition)
typedef void *device_t;
struct simplefb_softc { int unused; };
static struct simplefb_softc *simplefb_deferred;
static device_t simplefb_owner;
static bool simplefb_committed;
static unsigned attaches;
static void *device_private(void *p) { return p; }
static int simplefb_attach_genfb(struct simplefb_softc *sc) {
    assert(sc); attaches++; return 0;
}
C
simplefb="$src/sys/dev/fdt/simplefb.c"
extract simplefb_console_reserve "$simplefb" 'static bool' >> "$work/selection.c"
extract simplefb_console_commit "$simplefb" 'static void' >> "$work/selection.c"
extract simplefb_console_fallback "$simplefb" 'static int' >> "$work/selection.c"
cat >> "$work/selection.c" <<'C'
int main(void) {
    struct simplefb_softc sc;
    int gpu, other;
    assert(!simplefb_console_reserve(&gpu));
    simplefb_deferred=&sc;
    assert(simplefb_console_reserve(&gpu));
    assert(!simplefb_console_reserve(&other));
    simplefb_console_fallback(&sc); assert(attaches==1);
    simplefb_console_fallback(&sc); assert(attaches==1);
    simplefb_owner=NULL; simplefb_deferred=&sc;
    assert(simplefb_console_reserve(&gpu));
    simplefb_console_commit(&gpu);
    simplefb_console_fallback(&sc); assert(attaches==1);
    assert(!simplefb_console_reserve(&other));
    simplefb_committed=false; simplefb_owner=NULL; simplefb_deferred=&sc;
    simplefb_console_fallback(&sc); assert(attaches==2);
    puts("VirtGPU production early selection, single final console and failure fallback passed");
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/selection.c" -o "$work/selection"
"$work/selection"
cat > "$work/raster.c" <<'C'
#include <assert.h>
#include <stdio.h>
#undef putchar
#include <string.h>
typedef unsigned int u_int;
struct wsdisplay_emulops {
    void (*putchar)(void *, int, int, u_int, long);
    void (*cursor)(void *, int, int, int);
    void (*copycols)(void *, int, int, int, int);
    void (*erasecols)(void *, int, int, int, long);
    void (*copyrows)(void *, int, int, int);
    void (*eraserows)(void *, int, int, long);
};
struct rasops_info { void *ri_hw; };
struct vcons_screen {
    struct rasops_info scr_ri; struct wsdisplay_emulops scr_driver_ops;
    void *scr_cookie;
};
struct genfb_private { struct { void (*genfb_damage)(void *); } sc_ops; };
struct genfb_softc { struct genfb_private *sc_private; };
static int draws, notifications, font;
static void damage(void *sc) {
    assert(sc && draws == notifications + 1); notifications++;
}
static void glyph(void *r,int row,int col,u_int ch,long a) {
    assert(r && row==1 && col==2 && ch==3 && a==4); font=1; draws++;
}
static void glyph2(void *r,int row,int col,u_int ch,long a) {
    glyph(r,row,col,ch,a); font=2;
}
static void cursor(void *r,int on,int row,int col) {
    assert(r && on==1 && row==2 && col==3); draws++;
}
static void copycols(void *r,int row,int src,int dst,int n) {
    assert(r && row==1 && src==2 && dst==3 && n==4); draws++;
}
static void erasecols(void *r,int row,int col,int n,long a) {
    assert(r && row==1 && col==2 && n==3 && a==4); draws++;
}
static void copyrows(void *r,int src,int dst,int n) {
    assert(r && src==1 && dst==2 && n==3); draws++;
}
static void eraserows(void *r,int row,int n,long a) {
    assert(r && row==1 && n==2 && a==3); draws++;
}
C
for op in putchar cursor copycols erasecols copyrows eraserows; do
    extract "genfb_damage_$op" "$src/sys/dev/wsfb/genfb.c" 'static void' >> "$work/raster.c"
done
cat >> "$work/raster.c" <<'C'
int main(void) {
    struct genfb_private priv={ .sc_ops={damage} };
    struct genfb_softc sc={ &priv };
    struct vcons_screen s={ .scr_cookie=&sc,
        .scr_driver_ops={glyph,cursor,copycols,erasecols,copyrows,eraserows} };
    s.scr_ri.ri_hw=&s;
    genfb_damage_putchar(&s.scr_ri,1,2,3,4); assert(font==1);
    genfb_damage_cursor(&s.scr_ri,1,2,3);
    genfb_damage_copycols(&s.scr_ri,1,2,3,4);
    genfb_damage_erasecols(&s.scr_ri,1,2,3,4);
    genfb_damage_copyrows(&s.scr_ri,1,2,3);
    genfb_damage_eraserows(&s.scr_ri,1,2,3);
    struct vcons_screen s2=s; s2.scr_ri.ri_hw=&s2;
    s2.scr_driver_ops.putchar=glyph2;
    genfb_damage_putchar(&s2.scr_ri,1,2,3,4); assert(font==2);
    genfb_damage_putchar(&s.scr_ri,1,2,3,4); assert(font==1);
    assert(draws==8 && notifications==8);
    puts("genfb production glyph, cursor, copy, erase and per-screen font damage hooks passed");
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/raster.c" -o "$work/raster"
"$work/raster"
cat > "$work/redraw.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define WSDISPLAYIO_MODE_EMUL 0
struct genfb_softc;
struct genfb_mode_callback { bool (*gmc_setmode)(struct genfb_softc *,int); };
struct genfb_private {
    int sc_mode; struct genfb_mode_callback *sc_modecb;
    struct { void (*genfb_damage)(void *); } sc_ops;
};
struct genfb_softc {
    struct genfb_private *sc_private;
    struct { void *active; } vd;
};
static unsigned stage, redraws, damages;
static bool mode(struct genfb_softc *sc, int n) {
    assert(sc && n==0 && stage==0); stage=1; return true;
}
static void genfb_restore_palette(struct genfb_softc *sc) {
    assert(sc && stage==1); stage=2;
}
static void vcons_redraw_screen(void *active) {
    assert(active && stage==2); stage=3; redraws++;
}
static void damage(void *cookie) {
    struct genfb_softc *sc=cookie;
    assert(stage==(sc->vd.active ? 3 : 1)); stage=4; damages++;
}
C
extract genfb_restore_console "$src/sys/dev/wsfb/genfb.c" 'static void' >> "$work/redraw.c"
cat >> "$work/redraw.c" <<'C'
int main(void) {
    struct genfb_mode_callback cb={ mode };
    struct genfb_private priv={ .sc_mode=1, .sc_modecb=&cb,
        .sc_ops={ damage } };
    struct genfb_softc sc={ .sc_private=&priv, .vd={ &priv } };
    genfb_restore_console(&sc);
    assert(priv.sc_mode==0 && redraws==1 && damages==1 && stage==4);
    stage=0; genfb_restore_console(&sc);
    assert(priv.sc_mode==0 && redraws==2 && damages==2);
    stage=0; sc.vd.active=NULL; genfb_restore_console(&sc);
    assert(redraws==2 && damages==3 && stage==4);
    priv.sc_modecb=NULL; priv.sc_ops.genfb_damage=NULL;
    genfb_restore_console(&sc);
    puts("genfb production lastclose forces EMUL and marks damage after redraw passed");
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/redraw.c" -o "$work/redraw"
"$work/redraw"
cat > "$work/takeover.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define cpu_to_le32(x) (x)
#define IS_ERR(p) ((intptr_t)(p)<0 && (intptr_t)(p)>-4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define DRM_DEBUG(...) ((void)0)
#define WARN_ON(x) (x)
#define BUS_DMASYNC_POSTWRITE 1
#define VIRTIO_GPU_CMD_SET_SCANOUT 1
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 2
struct virtio_gpu_ctrl_hdr { uint32_t type; };
struct rectangle { uint32_t width,height,x,y; };
struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id,scanout_id;
    struct rectangle r;
};
struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    struct rectangle r;
};
union command {
    struct virtio_gpu_set_scanout scanout;
    struct virtio_gpu_resource_flush flush;
};
struct virtio_gpu_vbuffer { union command cmd; };
struct virtio_gpu_device {
    bool console_preparing, console_takeover, notify;
    int submit_error;
    struct { void *dmat; } *vdev;
};
struct virtio_gpu_object {
    bool dumb; unsigned hw_res_handle;
    struct { struct { size_t size; } base; } base;
    struct { struct { void *sg_dmamap; } *sgl; } *pages;
};
#define gem_to_virtio_gpu_obj(p) ((struct virtio_gpu_object *)(p))
struct format { unsigned cpp[1]; };
struct drm_framebuffer { void *obj[1]; struct format *format; unsigned pitches[1]; };
struct virtio_gpu_output { bool enabled; unsigned index; };
struct drm_crtc { struct virtio_gpu_output *output; };
#define drm_crtc_to_virtio_gpu_output(c) ((c)->output)
struct drm_plane_state {
    struct drm_framebuffer *fb; struct drm_crtc *crtc;
    unsigned src_w,src_h,src_x,src_y;
};
struct drm_device { void *dev_private; };
struct drm_plane { struct drm_device *dev; struct drm_plane_state *state; };
struct drm_rect { unsigned x1,y1,x2,y2; };
struct virtio_gpu_object_array { int unused; };
static struct virtio_gpu_object_array array;
static struct virtio_gpu_vbuffer buffer;
static unsigned allocations, transfers, scanouts, flushes, posts;
static unsigned fail_allocation;
static bool fail_array;
static int transfer_error, scanout_error;
static bool drm_atomic_helper_damage_merged(struct drm_plane_state *o,
    struct drm_plane_state *n, struct drm_rect *r) {
    (void)o; (void)n; *r=(struct drm_rect){0,0,32,32}; return true;
}
static void virtio_gpu_disable_notify(struct virtio_gpu_device *v) { v->notify=false; }
static void virtio_gpu_enable_notify(struct virtio_gpu_device *v) { v->notify=true; }
static void *virtio_gpu_alloc_cmd(struct virtio_gpu_device *v,
    struct virtio_gpu_vbuffer **out, size_t size) {
    (void)v; assert(size<=sizeof(buffer.cmd)); allocations++;
    if (allocations==fail_allocation) return ERR_PTR(-ENOMEM);
    *out=&buffer; return &buffer.cmd;
}
static int virtio_gpu_queue_sync(struct virtio_gpu_device *v,
    struct virtio_gpu_vbuffer *b, void *h, void *f) {
    (void)v; assert(b==&buffer && !h && !f);
    if (b->cmd.scanout.hdr.type==VIRTIO_GPU_CMD_SET_SCANOUT) {
        scanouts++; return scanout_error;
    }
    assert(b->cmd.flush.hdr.type==VIRTIO_GPU_CMD_RESOURCE_FLUSH);
    flushes++; return 0;
}
#define VIRTGPU_OPERATION_TO_HOST 2
static struct virtio_gpu_object_array *virtio_gpu_operation_array_alloc(
    struct virtio_gpu_device *v,unsigned n,int kind) {
    assert(v && n==1 && kind==VIRTGPU_OPERATION_TO_HOST); return fail_array ? NULL : &array;
}
static void virtio_gpu_array_add_obj(struct virtio_gpu_object_array *a, void *o) {
    assert(a==&array && o);
}
static int virtio_gpu_cmd_transfer_to_host_2d(struct virtio_gpu_device *v,
    uint64_t off, unsigned w, unsigned h, unsigned x, unsigned y,
    struct virtio_gpu_object_array *a, void *f) {
    (void)v; assert(!off && w==32 && h==32 && !x && !y && a==&array && !f);
    transfers++; return transfer_error;
}

C
vq="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_vq.c"
plane="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_plane.c"
extract virtio_gpu_cmd_set_scanout "$vq" 'static int' >> "$work/takeover.c"
extract virtio_gpu_cmd_resource_flush "$vq" 'static int' >> "$work/takeover.c"
extract virtio_gpu_update_dumb_bo "$plane" 'static int' >> "$work/takeover.c"
extract virtio_gpu_primary_plane_update "$plane" 'static void' >> "$work/takeover.c"
extract virtgpu_console_fallback_safe "$console" 'static bool' >> "$work/takeover.c"
cat >> "$work/takeover.c" <<'C'
static void reset(struct virtio_gpu_device *v) {
    v->console_preparing=true; v->console_takeover=false;
    v->submit_error=0; v->notify=true;
    allocations=transfers=scanouts=flushes=posts=0;
    fail_allocation=0; fail_array=false; transfer_error=scanout_error=0;
}
int main(void) {
    struct virtio_gpu_device v={0};
    __typeof__(*v.vdev) vd={0}; v.vdev=&vd;
    struct virtio_gpu_object bo={ .dumb=true, .hw_res_handle=1 };
    __typeof__(*bo.pages) pages; __typeof__(*pages.sgl) sg={0};
    pages.sgl=&sg; bo.pages=&pages;
    struct format fmt={{4}};
    struct drm_framebuffer fb={ .obj={&bo}, .format=&fmt, .pitches={128} };
    struct virtio_gpu_output out={ .enabled=true };
    struct drm_crtc crtc={&out};
    struct drm_plane_state state={ .fb=&fb, .crtc=&crtc, .src_w=32<<16, .src_h=32<<16 };
    struct drm_plane_state old={ .crtc=&crtc };
    struct drm_device dev={&v}; struct drm_plane p={&dev,&state};
    reset(&v);
    assert(!virtio_gpu_cmd_set_scanout(&v,0,0,32,32,0,0));
    assert(!allocations && !scanouts && virtgpu_console_fallback_safe(&v));
    fail_array=true; virtio_gpu_primary_plane_update(&p,&old);
    assert(v.submit_error==-ENOMEM && !transfers && !scanouts && !flushes);
    assert(v.notify && virtgpu_console_fallback_safe(&v));
    reset(&v); transfer_error=-ENOMEM; virtio_gpu_primary_plane_update(&p,&old);
    assert(transfers==1 && posts==0 && !scanouts && !flushes && !allocations);
    assert(v.submit_error==-ENOMEM && v.notify && virtgpu_console_fallback_safe(&v));
    reset(&v); fail_allocation=1; virtio_gpu_primary_plane_update(&p,&old);
    assert(transfers==1 && !scanouts && !flushes && virtgpu_console_fallback_safe(&v));
    reset(&v); fail_allocation=2; virtio_gpu_primary_plane_update(&p,&old);
    assert(transfers==1 && scanouts==1 && !flushes && v.submit_error==-ENOMEM);
    assert(v.notify && !virtgpu_console_fallback_safe(&v));
    reset(&v); scanout_error=-ETIMEDOUT; virtio_gpu_primary_plane_update(&p,&old);
    assert(scanouts==1 && !flushes && !virtgpu_console_fallback_safe(&v));
    reset(&v); virtio_gpu_primary_plane_update(&p,&old);
    assert(transfers==1 && scanouts==1 && flushes==1 && !v.submit_error && v.notify);
    assert(!virtgpu_console_fallback_safe(&v));
    puts("VirtGPU production plane prerequisite, scanout boundary and post-takeover failures passed");
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/takeover.c" -o "$work/takeover"
"$work/takeover"
