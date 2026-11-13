/* SPDX-License-Identifier: BSD-2-Clause */
/* Native pinch contracts use the production pointer and gesture policies. */
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/hid/hidmtvar.h>
size_t hidmt_test_allocations;
static unsigned checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } checks++; } while (0)
struct rig {
 struct hidmt_pointer p;
 struct hidmt_gstate g;
 struct hidmt_gconf gc;
 struct hidmt_scroll sc;
 struct hidmt_frame f;
 uint64_t t;
};
static void init(struct rig *r)
{
 memset(r, 0, sizeof(*r));
 r->gc.enable = true;
 r->sc = (struct hidmt_scroll){.enable=true,.natural=true,.scale=112,.range=3000};
 r->f.ncontacts = 2;
 r->f.contacts[0] = (struct hidmt_contact){.id=1,.x=1000,.y=1000};
 r->f.contacts[1] = (struct hidmt_contact){.id=2,.x=2000,.y=1000};
}
static struct hidmt_motion step(struct rig *r)
{
 struct hidmt_motion m;
 r->t += 8000;
 hidmt_pointer_update_scroll(&r->p, &r->f, r->t,
     HIDMT_TAP_LEFT | HIDMT_TAP_RIGHT, &r->sc, &m);
 hidmt_gesture_update(&r->g, &r->gc, &r->f, &m);
 return m;
}
static void quiet(struct hidmt_motion m)
{
 CHECK(m.dx == 0 && m.dy == 0 && m.scroll_x == 0 && m.scroll_y == 0);
 CHECK(!m.tap && m.tap_button == 0);
}
static struct hidmt_motion begin(struct rig *r)
{
 struct hidmt_motion m;
 init(r); m=step(r); CHECK(m.gesture==0);
 r->f.contacts[0].x=800; r->f.contacts[1].x=2200;
 m=step(r);
 CHECK((m.gesture & HIDMT_GESTURE_BEGIN)!=0);
 CHECK(m.gesture_fingers==2);
 CHECK((m.gesture & HIDMT_GESTURE_UPDATE)!=0);
 CHECK(m.gesture_scale==91750); /* 1.4 in 16.16, truncated */
 quiet(m);
 return m;
}
int main(void)
{
 struct rig r;
 struct hidmt_motion m;
 struct hidmt_contact tmp;
 begin(&r);
 r.f.contacts[0].x=1100; r.f.contacts[1].x=1900;
 m=step(&r); CHECK(m.gesture==HIDMT_GESTURE_UPDATE);
 CHECK(m.gesture_scale==52428); quiet(m);
 /* Report ordering is not contact replacement. */
 tmp=r.f.contacts[0];r.f.contacts[0]=r.f.contacts[1];r.f.contacts[1]=tmp;
 m=step(&r); CHECK(m.gesture==0);quiet(m);
 r.f.ncontacts=1; m=step(&r);CHECK(m.gesture==HIDMT_GESTURE_END);quiet(m);
 r.f.contacts[0].x+=500;m=step(&r);CHECK(m.gesture==0);quiet(m);
 r.f.ncontacts=0;m=step(&r);CHECK(!m.tap && m.gesture==0);
 /* A new sequence starts after every finger lifts. */
 begin(&r);r.f.ncontacts=0;m=step(&r);CHECK(m.gesture==HIDMT_GESTURE_END);quiet(m);
 /* Translation chooses scrolling. Later separation must not steal it. */
 init(&r);step(&r);r.f.contacts[0].y+=100;r.f.contacts[1].y+=100;
 m=step(&r);CHECK(m.gesture==0);
 r.f.contacts[0].y+=200;r.f.contacts[1].y+=200;
 m=step(&r);CHECK(m.gesture==0 && m.scroll_y!=0);
 r.f.contacts[0].x-=400;r.f.contacts[1].x+=400;
 m=step(&r);CHECK(m.gesture==0);
 /* Single-contact pointer motion remains available. */
 init(&r);r.f.ncontacts=1;step(&r);r.f.contacts[0].x+=200;
 m=step(&r);CHECK(m.dx!=0 && m.gesture==0);
 /* Contact replacement cancels, without starting a second gesture. */
 begin(&r);r.f.contacts[1].id=9;m=step(&r);
 CHECK(m.gesture==HIDMT_GESTURE_CANCEL);quiet(m);
 r.f.contacts[1].x+=500;m=step(&r);CHECK(m.gesture==0);quiet(m);
 /* Extra fingers cancel a pinch and remain quiet until release. */
 begin(&r);r.f.ncontacts=3;r.f.contacts[2]=(struct hidmt_contact){.id=3,.x=3000,.y=2000};
 m=step(&r);CHECK(m.gesture==HIDMT_GESTURE_CANCEL);quiet(m);
 r.f.contacts[0].x+=300;r.f.contacts[1].x+=300;r.f.contacts[2].x+=300;
 m=step(&r);CHECK(m.gesture==0);quiet(m);
 /* Disabling and reset cancel once. */
 begin(&r);r.gc.enable=false;m=step(&r);CHECK(m.gesture==HIDMT_GESTURE_CANCEL);
 m=step(&r);CHECK(m.gesture==0);
 begin(&r);CHECK(hidmt_release(&r.p,&r.g)==HIDMT_GESTURE_CANCEL);
 CHECK(hidmt_release(&r.p,&r.g)==0);
 /* Physical click interrupts a pinch. */
 begin(&r);r.f.buttons=1;m=step(&r);CHECK(m.gesture==HIDMT_GESTURE_CANCEL);quiet(m);
 /* A common anchored pinch must not turn into scrolling at the lower threshold. */
 init(&r);step(&r);r.f.contacts[1].x+=40;m=step(&r);
 CHECK((m.gesture & HIDMT_GESTURE_BEGIN)!=0);quiet(m);
 r.f.contacts[1].x+=160;m=step(&r);
 CHECK(m.gesture==HIDMT_GESTURE_UPDATE && m.gesture_scale==78643);quiet(m);
 /* Coincident contacts must not divide by zero or start a pinch. */
 init(&r);r.f.contacts[1].x=1000;step(&r);r.f.contacts[1].x=2000;
 m=step(&r);CHECK(m.gesture==0);
 printf("hidmt pinch: %u checks PASS\n", checks);
 return 0;
}
