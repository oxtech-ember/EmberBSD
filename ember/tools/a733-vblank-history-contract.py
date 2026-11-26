#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD, AI-assisted. Exercise the real history and emit logic.
"""Build a host fixture against the driver's history and commit helpers."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / "sys/arch/arm/sunxi"
atomic = (source / "sun60i_a733_drm_atomic.c").read_text()
scanout = (source / "sun60i_a733_de352_scanout.h").read_text()
begin = scanout.index("struct sun60i_a733_de352_completion {")
end = scanout.index("};", begin) + 2
completion = scanout[begin:end]
flip = (root / "sys/external/bsd/drm2/drm/ember_drm_flip.c").read_text()
begin = flip.index("void\nember_drm_flip_finish")
end = flip.index("int\nember_drm_flip_wait_producer", begin)
emit = flip[begin:end]
begin = atomic.index("static int\na733_presented_stamp")
end = atomic.index("static int\na733_submit_frame", begin)
resolve = atomic[begin:end]

FIXTURE = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "sun60i_a733_vblank_history.h"

struct drm_pending_event {
 int *fence;
 int *completion;
 void (*completion_release)(int *);
};
struct drm_pending_vblank_event {
 struct drm_pending_event base;
 int pipe;
 struct { struct { uint32_t sequence, tv_sec, tv_usec; } vbl; } event;
};
struct drm_crtc_state { struct drm_pending_vblank_event *event; };
struct drm_device { unsigned event_lock; };
struct drm_crtc { struct drm_device *dev; unsigned index; };
struct a733_drm {
 struct a733_vblank_history history;
 struct drm_device *dev;
 struct drm_crtc crtc;
 struct a733_drm *client;
};
#define spin_lock_irqsave(lock, flags) do { assert(!(*lock)++); (flags)=0; } while (0)
#define spin_unlock_irqrestore(lock, flags) do { assert((*lock)-- == 1); (void)(flags); } while (0)
static unsigned sent, canceled;
static unsigned fallback_calls;
static uint64_t observed[2], recorded[2];
static int fallback_error;
static int drm_crtc_index(struct drm_crtc *crtc) { return crtc->index; }
static void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *p)
{ assert(dev->event_lock != 0 && p != NULL); sent++; }
static void drm_event_cancel_free(struct drm_device *dev, struct drm_pending_event *p)
{ assert(dev->event_lock == 0 && p != NULL); canceled++; }
static void dma_fence_set_error(int *fence, int error) { *fence = error; }
static int dma_fence_signal(int *fence) { assert(*fence != 0); return 0; }
static void complete_all(int *completion) { (*completion)++; }
static bool a733_drm_vblank_stamp(struct a733_drm *sc, uint64_t raw, struct a733_vblank_stamp *out)
{ return a733_history_lookup(&sc->history, raw, out); }
static void record(struct a733_vblank_history *h, uint64_t raw, uint64_t seq, uint64_t ns)
{ a733_history_record(h, h->generation, raw, seq - 1, (int64_t)ns - 1, true, seq, ns); }
"""

MOCK_OBSERVE = r"""
static int sun60i_a733_de352_observe_current(struct a733_drm *sc,
    struct sun60i_a733_de352_completion *out)
{
 unsigned call = fallback_calls++;
 assert(call < 2);
 if (fallback_error)
  return fallback_error;
 out->observed_sequence = observed[call];
 out->observed_time_ns = 1; /* Never used for the DRM timestamp. */
 if (recorded[call])
  record(&sc->history, observed[call], recorded[call], 7000123456);
 return 0;
}
"""

TESTS = r"""
static void history_tests(void)
{
 struct a733_vblank_history h = {0};
 struct a733_vblank_stamp stamp = { 0 };
 uint64_t epoch;
 record(&h, 1, 1, 1);
 assert(!a733_history_lookup(&h, 1, &stamp));
 a733_history_reset(&h, true);
 record(&h, 1, 9, 100);
 record(&h, 2, 10, 110);
 assert(a733_history_lookup(&h, 1, &stamp) && stamp.sequence == 9 && stamp.time_ns == 100);
 assert(!a733_history_lookup(&h, 3, &stamp));
 record(&h, 4, 12, 130);
 assert(!a733_history_lookup(&h, 2, &stamp) && !a733_history_lookup(&h, 4, &stamp));
 record(&h, 5, 13, 140);
 assert(a733_history_lookup(&h, 5, &stamp));
 a733_history_record(&h, h.generation, 6, 13, 140, true, 14, 140);
 assert(!a733_history_lookup(&h, 6, &stamp));
 a733_history_record(&h, h.generation, 7, 14, 140, true, 14, 150);
 assert(!a733_history_lookup(&h, 7, &stamp));
 a733_history_record(&h, h.generation, 8, 14, 140, false, 15, 150);
 assert(!a733_history_lookup(&h, 8, &stamp));
 for (unsigned i = 9; i <= 30; i++)
  record(&h, i, i+20, i+200);
 assert(a733_history_lookup(&h, 15, &stamp));
 assert(!a733_history_lookup(&h, 14, &stamp));
 epoch = h.generation;
 a733_history_reset(&h, false);
 assert(!a733_history_lookup(&h, 30, &stamp));
 a733_history_reset(&h, true);
 a733_history_record(&h, epoch, 30, 50, 20, true, 51, 21);
 assert(!a733_history_lookup(&h, 30, &stamp));
 record(&h, UINT32_MAX, UINT32_MAX, 500);
 record(&h, UINT64_C(1) + UINT32_MAX, UINT64_C(1) + UINT32_MAX, 520);
 assert(a733_history_lookup(&h, UINT32_MAX, &stamp));
 assert(a733_history_lookup(&h, UINT64_C(1) + UINT32_MAX, &stamp));
 a733_history_reset(&h, true);
 record(&h, UINT64_MAX, UINT64_MAX, 600);
 record(&h, 1, 1, 610);
 assert(!a733_history_lookup(&h, 1, &stamp));
 a733_history_reset(&h, true);
 a733_history_record(&h, h.generation, 1, UINT64_MAX, 0, true, 0, 630);
 assert(!a733_history_lookup(&h, 1, &stamp));
}

static void event_tests(void)
{
 struct drm_device dev = {0};
 struct a733_drm sc = { .dev = &dev, .crtc = {&dev, 3} };
 struct drm_pending_vblank_event ev = {0}, *event = &ev;
 struct sun60i_a733_de352_completion done = { .observed_sequence=20,
     .observed_time_ns=100, .shadow_fast=true };
 struct a733_vblank_stamp stamp;
 int fence=0, completion=0;

 sc.client = &sc;
 a733_history_reset(&sc.history, true);
 record(&sc.history, 20, UINT64_C(0x100000009), 3000123456);
 record(&sc.history, 21, UINT64_C(0x10000000a), 3017000000);
 assert(a733_presented_stamp(&sc,&done,&stamp)==0 && fallback_calls==0);
 ember_drm_flip_finish(&sc.crtc,&event,stamp.sequence,stamp.time_ns,0);
 assert(sent==1 && canceled==0 && ev.pipe==3);
 assert(ev.event.vbl.sequence==9 && ev.event.vbl.tv_sec==3 && ev.event.vbl.tv_usec==123);
 ember_drm_flip_finish(&sc.crtc,&event,stamp.sequence,stamp.time_ns,0);
 assert(sent==1);

 a733_history_reset(&sc.history,true);
 observed[0]=22;
 observed[1]=24;
 recorded[1]=42;
 assert(a733_presented_stamp(&sc,&done,&stamp)==0 && fallback_calls==2);
 assert(stamp.raw==24 && stamp.sequence==42 && stamp.time_ns==7000123456);
 a733_history_reset(&sc.history,true);
 fallback_calls=0;
 recorded[1]=0;
 assert(a733_presented_stamp(&sc,&done,&stamp)==ETIMEDOUT && fallback_calls==2);
 event = &ev;
 ev.base.fence=&fence;
 ev.base.completion=&completion;
 ember_drm_flip_finish(&sc.crtc,&event,stamp.sequence,stamp.time_ns,ETIMEDOUT);
 assert(sent==1 && canceled==1 && fence==-ETIMEDOUT && completion==1);
 fallback_calls=0;
 observed[0]=20;
 assert(a733_presented_stamp(&sc,&done,&stamp)==EPROTO && fallback_calls==1);
 fallback_calls=0;
 fallback_error=EIO;
 assert(a733_presented_stamp(&sc,&done,&stamp)==EIO && fallback_calls==1);
}

int main(void)
{
 history_tests();
 event_tests();
 puts("A733 vblank history and exact event contract passed");
}
"""

with tempfile.TemporaryDirectory(prefix="a733-vblank-history-") as tmp:
    c_file = Path(tmp) / "contract.c"
    exe = Path(tmp) / "contract"
    c_file.write_text(FIXTURE + completion + MOCK_OBSERVE + emit + resolve + TESTS)
    cc = shlex.split(os.environ.get("CC", "cc"))
    cflags = shlex.split(os.environ.get("A733_VBLANK_CFLAGS", ""))
    subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(source)] +
                   cflags + [str(c_file), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
