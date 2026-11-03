/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; exercise the actual AXP803 DCDC4 regulator path. */
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
typedef unsigned int u_int;
typedef struct fake_bus *i2c_tag_t;
typedef unsigned int i2c_addr_t;
typedef struct fake_device *device_t;
#define __BIT(n) (1U << (n))
#define __SHIFTIN(v,m) ((v) & (m))
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); exit(1); } } while (0)
struct axppmic_ctrl {
 const char *c_name; u_int c_min,c_max,c_step1,c_step1cnt,c_step2,c_step2cnt,c_step2start;
 uint8_t c_voltage_reg,c_voltage_mask;
};
struct axppmic_config { int unused; };
static const struct axppmic_config axp803_config = {0};
struct axppmic_softc { const struct axppmic_config *sc_conf; };
struct axpreg_softc { device_t sc_dev; i2c_tag_t sc_i2c; i2c_addr_t sc_addr; const struct axppmic_ctrl *sc_ctrl; };
struct fake_device { void *private; device_t parent; };
struct fake_bus { uint8_t reg[256]; int locked, reads, writes, acquires, acquire_fail, read_fail, write_fail, poll_done, poll_bad, frozen, slow_read; uint8_t written; long ns; };
static struct fake_bus *current;
static void *device_private(device_t d) { return d->private; }
static device_t device_parent(device_t d) { return d->parent; }
static int iic_acquire_bus(i2c_tag_t b,int flags) { (void)flags; CHECK(!b->locked); if (++b->acquires == b->acquire_fail) return EIO; b->locked=1; return 0; }
static void iic_release_bus(i2c_tag_t b,int flags) { (void)flags; CHECK(b->locked); b->locked=0; }
static int axppmic_read(i2c_tag_t b,i2c_addr_t a,uint8_t reg,uint8_t *v,int flags) {
 (void)a;(void)flags; CHECK(b->locked); if (++b->reads == b->read_fail) return EIO;
 if (b->writes && b->slow_read) b->ns += 100000000;
 *v=b->reg[reg]; if (reg==0x23 && b->writes && b->poll_done && b->reads>=b->poll_done) { *v |= 0x80; if (b->poll_bad) *v ^= 1; }
 return 0;
}
static int axppmic_write(i2c_tag_t b,i2c_addr_t a,uint8_t reg,uint8_t v,int flags) {
 (void)a;(void)flags; CHECK(b->locked); b->writes++; b->written=v; if (b->write_fail) return EIO;
 b->reg[reg]=v & 0x7f; return 0;
}
static void delay(unsigned n) { CHECK(!current->locked); current->ns += (long)n*1000; }
static void getnanouptime(struct timespec *t) { t->tv_sec=current->frozen ? 0 : current->ns/1000000000L; t->tv_nsec=current->frozen ? 0 : current->ns%1000000000L; }
#include "axp803-actual.inc"
int main(void) {
 struct fake_bus b; struct axppmic_softc parent={&axp803_config};
 struct axppmic_ctrl ctrl={"dcdc4",500,1300,10,70,20,5,0,0x23,0x7f};
 struct axpreg_softc sc={0}; struct fake_device pdev={&parent,NULL}, dev={&sc,&pdev};
 sc.sc_dev=&dev;sc.sc_i2c=&b;sc.sc_addr=0x34;sc.sc_ctrl=&ctrl;current=&b;
 memset(&b,0,sizeof(b));b.reg[3]=0x40;b.reg[0x27]=0x10;b.reg[0x23]=0x2c;
 CHECK(axpreg_set_voltage(&dev,950000,950000)==EOPNOTSUPP && b.writes==0 && !b.locked);
 memset(&b,0,sizeof(b));b.reg[3]=0x41;b.reg[0x23]=0x2c;
 CHECK(axpreg_set_voltage(&dev,950000,950000)==EOPNOTSUPP && b.writes==0 && !b.locked);
 memset(&b,0,sizeof(b));b.reg[3]=0x41;b.reg[0x27]=0x10;b.reg[0x23]=0xac;b.poll_done=4;
 CHECK(axpreg_set_voltage(&dev,950000,950000)==0 && b.writes==1 && b.written==0xad && b.reg[0x23]==0x2d && b.ns>=100000 && !b.locked);
 memset(&b,0,sizeof(b));b.reg[3]=0x41;b.reg[0x27]=0x10;b.reg[0x23]=0x2c;b.write_fail=1;
 CHECK(axpreg_set_voltage(&dev,950000,950000)==EIO && b.writes==1 && !b.locked);
 for (int failure=1; failure<=2; failure++) {
  memset(&b,0,sizeof(b)); b.reg[3]=0x41;b.reg[0x27]=0x10;b.reg[0x23]=0x2c;b.acquire_fail=failure;
  CHECK(axpreg_set_voltage(&dev,950000,950000)==EIO && b.writes==(failure==2) && !b.locked);
 }
 for (int failure=1; failure<=4; failure++) {
  memset(&b,0,sizeof(b)); b.reg[3]=0x41;b.reg[0x27]=0x10;b.reg[0x23]=0x2c;b.read_fail=failure;
  CHECK(axpreg_set_voltage(&dev,950000,950000)==EIO && b.writes==(failure==4) && !b.locked);
 }
 memset(&b,0,sizeof(b));b.reg[3]=0x41;b.reg[0x27]=0x10;b.reg[0x23]=0x2c;b.poll_done=4;b.poll_bad=1;
 CHECK(axpreg_set_voltage(&dev,950000,950000)==EIO && b.writes==1 && !b.locked);
 memset(&b,0,sizeof(b));b.reg[3]=0x41;b.reg[0x27]=0x10;b.reg[0x23]=0x2c;
 CHECK(axpreg_set_voltage(&dev,950000,950000)==ETIMEDOUT && b.writes==1 && b.ns>=100000000 && !b.locked);
 memset(&b,0,sizeof(b));b.reg[3]=0x41;b.reg[0x27]=0x10;b.reg[0x23]=0x2c;b.poll_done=4;b.slow_read=1;
 CHECK(axpreg_set_voltage(&dev,950000,950000)==ETIMEDOUT && b.writes==1 && !b.locked);
 memset(&b,0,sizeof(b));b.reg[3]=0x41;b.reg[0x27]=0x10;b.reg[0x23]=0x2c;b.frozen=1;
 CHECK(axpreg_set_voltage(&dev,950000,950000)==ETIMEDOUT && b.writes==1 && b.reads==1003 && !b.locked);
 /* A different rail and a different chip keep the existing generic route. */
 memset(&b,0,sizeof(b)); b.reg[0x23]=0x2c; ctrl.c_name="dcdc3";
 CHECK(axpreg_set_voltage(&dev,950000,950000)==0 && b.writes==1 && b.reg[0x23]==45);
 { static const struct axppmic_config other = {0};
   memset(&b,0,sizeof(b)); b.reg[0x23]=0x2c; ctrl.c_name="dcdc4"; parent.sc_conf=&other;
   CHECK(axpreg_set_voltage(&dev,950000,950000)==0 && b.writes==1 && b.reg[0x23]==45);
 }
 puts("AXP803 DCDC4 exact preparation: PASS");return 0;
}
