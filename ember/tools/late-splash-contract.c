/* SPDX-License-Identifier: BSD-2-Clause */
/* Real attachment and screen initialization; no hardware or kernel claim. */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "late splash:%d: %s\n", __LINE__, #x); \
			exit(1);                                               \
		}                                                              \
	} while (0)
#define KASSERT CHECK
#define KASSERTMSG(x, ...) CHECK(x)
#define RB_SINGLE 2
#define RB_USERCONF 0x1000
#define RB_ASKNAME 1
#define AB_VERBOSE 0x20000
#define AB_DEBUG 0x80000
#define AB_SILENT 0x10000
#define AB_QUIET 0x100000
#include "splash-mask.h"
#ifdef SPLASHSCREEN
#define GENFB_DISABLE_TEXT
#endif
#define DISABLESPLASH (boothowto & SPLASH_BOOT_DISABLE_MASK)
#define CN_NULL 0
#define RI_CENTER 1
#define RI_FULLCLEAR 2
#define RI_CLEAR 4
#define RI_ENABLE_ALPHA 8
#define RI_8BIT_IS_RGB 16
#define VCONS_LOADFONT 1
#define VCONS_DONT_READ 2
#define VCONS_SCREEN_IS_STATIC 4
#define WSSCREEN_WSCOLORS 1
#define WSSCREEN_HILIT 2
#define WSSCREEN_UNDERLINE 4
#define WSSCREEN_RESIZE 8
#define WSDISPLAYIO_MODE_EMUL 0
#define KM_SLEEP 0
#define BUS_SPACE_MAP_LINEAR 1
#define BUS_SPACE_MAP_PREFETCHABLE 2
#define PRIxBUSADDR PRIx64
#define SIMPLEFB_FORMAT_R8G8B8A8 1
#define SIMPLEFB_FORMAT_B8G8R8X8 2
#define SIMPLEFB_FORMAT_X2R10G10B10 3
#define SPLASH_CMAP_OFFSET 48
#define SPLASH_CMAP_SIZE 64
#define SPLASH_F_CENTER 1
#define SPLASH_F_FILL 2
#define uimin(a, b) ((a) < (b) ? (a) : (b))
#define CFARGS(...) 0
#define SCREEN_DISABLE_DRAWING(s) ((s)->disabled = true)
#define SCREEN_ENABLE_DRAWING(s) ((s)->disabled = false)
typedef uintptr_t paddr_t, vaddr_t, bus_space_handle_t;
typedef uint64_t bus_addr_t, bus_size_t;
struct properties {
	bool console, preserve, clear, has_clear;
	uint32_t width, height, stride;
	uint8_t depth;
	uint64_t address, virtual_address;
};
struct device {
	struct properties props;
};
typedef struct device *device_t;
typedef struct properties *prop_dictionary_t;
struct font {
	int fontwidth, fontheight;
};
struct emulops {
	void (*eraserows)(void *, int, int, long);
	void (*putchar)(void);
	void (*cursor)(void);
	void (*copycols)(void);
	void (*erasecols)(void);
	void (*copyrows)(void);
};
struct rasops_info {
	int ri_depth, ri_width, ri_height, ri_stride, ri_flg, ri_rows, ri_cols,
	    ri_caps;
	int ri_rnum, ri_gnum, ri_bnum, ri_rpos, ri_gpos, ri_bpos;
	void *ri_hw, *ri_bits, *ri_hwbits, *ri_origbits;
	uint32_t ri_devcmap[16];
	struct font *ri_font;
	struct emulops ri_ops;
};
struct vcons_screen {
	struct rasops_info scr_ri;
	int scr_flags;
	bool disabled;
	struct emulops scr_driver_ops;
};
struct wsscreen_descr {
	const char *name;
	int nrows, ncols;
	void *textops;
	int fontwidth, fontheight, capabilities;
	void *modecookie;
};
struct wsscreen_list {
	int nscreens;
	const struct wsscreen_descr **screens;
};
struct wsdisplay_accessops {
	void (*ioctl)(void);
	void (*mmap)(void);
	void (*pollc)(void);
};
struct vcons_data {
	void *cookie;
	void (*init_screen)(void *, struct vcons_screen *, int, long *);
};
struct genfb_softc;
struct genfb_ops {
	void (*genfb_ioctl)(void);
	void (*genfb_mmap)(void);
	void (*genfb_damage)(struct genfb_softc *);
};
struct modecb {
	void (*gmc_setmode)(struct genfb_softc *, int);
};
struct splash_info {
	int si_depth, si_width, si_height, si_stride;
	void *si_bits, *si_hwbits, *si_fillrect;
};
struct genfb_private {
	struct genfb_ops sc_ops;
	struct vcons_screen sc_console_screen;
	struct wsscreen_descr sc_defaultscreen_descr;
	const struct wsscreen_descr *sc_screens[1];
	struct wsscreen_list sc_screenlist;
	struct modecb *sc_modecb;
	void *sc_cmcb;
	uint32_t *sc_devcmap;
	void *sc_shadowfb;
	bool sc_enable_shadowfb, sc_want_clear, sc_palette_ready;
	int sc_mode;
	uint8_t sc_cmap_red[256], sc_cmap_green[256], sc_cmap_blue[256];
	struct splash_info sc_splash;
	struct wsdisplay_accessops sc_accessops;
};
struct genfb_softc {
	device_t sc_dev;
	struct genfb_private *sc_private;
	int sc_width, sc_height, sc_depth, sc_stride;
	size_t sc_fbsize;
	uintptr_t sc_fboffset;
	void *sc_fbaddr;
	struct vcons_data vd;
};
struct simplefb_framebuffer {
	uint32_t sf_width, sf_height, sf_stride;
	uint8_t sf_depth;
	int sf_format;
	bus_addr_t sf_address;
	bus_size_t sf_size;
};
struct simplefb_softc {
	struct genfb_softc sc_gen;
	int sc_phandle, sc_framebuffer_error;
	struct simplefb_framebuffer sc_framebuffer;
	void *sc_bst;
	bus_space_handle_t sc_bsh;
	bus_addr_t sc_paddr;
};
struct wsemuldisplaydev_attach_args {
	bool console;
	struct wsscreen_list *scrdata;
	struct wsdisplay_accessops *accessops;
	void *accesscookie;
};
struct consdev {
	int cn_pri;
	void (*cn_putc)(void);
	void (*cn_getc)(void);
	void (*cn_pollc)(void);
};
static void noop(void)
{
}
static void wsdisplay_cnputc(void)
{
}
static struct consdev uart, *cn_tab;
static int boothowto, simplefb_console_phandle = -1;
static unsigned clears, splashes, replays, attaches, palettes;
static bool shadow;
static bool fail_map;
static unsigned char pixels[64], original[64];
#ifdef SPLASHSCREEN
static unsigned char rasops_cmap[768];
#endif
static struct font font = {2, 2};
static struct genfb_softc *genfb_softc;
static prop_dictionary_t device_properties(device_t d)
{
	return &d->props;
}
static void prop_dictionary_set_bool(prop_dictionary_t p, const char *n, bool v)
{
	if (!strcmp(n, "is_console"))
		p->console = v;
	else if (!strcmp(n, "preserve-splash"))
		p->preserve = v;
	else if (!strcmp(n, "clear-screen")) {
		p->clear = v;
		p->has_clear = true;
	}
}
static bool prop_dictionary_get_bool(prop_dictionary_t p, const char *n,
				     bool *v)
{
	if (!strcmp(n, "is_console"))
		*v = p->console;
	else if (!strcmp(n, "preserve-splash"))
		*v = p->preserve;
	else if (!strcmp(n, "clear-screen") && p->has_clear)
		*v = p->clear;
	else
		return false;
	return true;
}
static bool prop_dictionary_get_uint16(prop_dictionary_t p, const char *n,
				       uint16_t *v)
{
	(void)p;
	(void)n;
	(void)v;
	return false;
}
static void prop_dictionary_set_uint32(prop_dictionary_t p, const char *n,
				       uint32_t v)
{
	if (!strcmp(n, "width"))
		p->width = v;
	else if (!strcmp(n, "height"))
		p->height = v;
	else if (!strcmp(n, "linebytes"))
		p->stride = v;
}
static void prop_dictionary_set_uint8(prop_dictionary_t p, const char *n,
				      uint8_t v)
{
	(void)n;
	p->depth = v;
}
static void prop_dictionary_set_uint64(prop_dictionary_t p, const char *n,
				       uint64_t v)
{
	if (!strcmp(n, "address"))
		p->address = v;
	else
		p->virtual_address = v;
}
static int bus_space_map(void *t, bus_addr_t a, bus_size_t s, int f,
			 bus_space_handle_t *h)
{
	(void)t;
	(void)a;
	(void)s;
	(void)f;
	*h = (uintptr_t)pixels;
	return fail_map;
}
static void *bus_space_vaddr(void *t, bus_space_handle_t h)
{
	(void)t;
	return (void *)h;
}
static void *kmem_alloc(size_t n, int f)
{
	(void)f;
	return malloc(n);
}
static void erase(void *v, int r, int n, long a)
{
	(void)v;
	(void)r;
	(void)n;
	(void)a;
	clears++;
	memset(pixels, 0, sizeof(pixels));
}
static int genfb_calc_cols(struct genfb_softc *s, struct rasops_info *r)
{
	(void)s;
	(void)r;
	return 2;
}
static void rasops_init(struct rasops_info *r, int a, int b)
{
	(void)a;
	(void)b;
	r->ri_font = &font;
	r->ri_rows = 2;
	r->ri_cols = 2;
	r->ri_origbits = r->ri_bits;
	r->ri_ops.eraserows = erase;
	if (r->ri_flg & RI_CLEAR)
		erase(r, 0, 2, 0);
}
static void rasops_reconfig(struct rasops_info *r, int a, int b)
{
	(void)r;
	(void)a;
	(void)b;
}
static void rasops_set_cmap(struct rasops_info *r, int a, int b, void *c,
			    void *d, void *e)
{
	(void)r;
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	(void)e;
}
static void genfb_restore_palette(struct genfb_softc *s)
{
	(void)s;
	palettes++;
}
static void genfb_init_palette(struct genfb_softc *s)
{
	(void)s;
}
static void vcons_init(struct vcons_data *v, void *c, void *s, void *o)
{
	(void)s;
	(void)o;
	v->cookie = c;
}
static void vcons_init_screen(struct vcons_data *v, struct vcons_screen *s,
			      int e, long *a)
{
	*a = 0;
	v->init_screen(v->cookie, s, e, a);
}
static void vcons_redraw_screen(struct vcons_screen *s)
{
	if (!s->disabled) {
		replays++;
		erase(s, 0, 2, 0);
	}
}
static void vcons_replay_msgbuf(struct vcons_screen *s)
{
	(void)s;
	replays++;
}
static void splash_get_cmap(int i, uint8_t *r, uint8_t *g, uint8_t *b)
{
	(void)i;
	*r = *g = *b = 0;
}
static int splash_render(struct splash_info *s, int f)
{
	(void)s;
	CHECK(f == (SPLASH_F_CENTER | SPLASH_F_FILL));
	splashes++;
	memset(pixels, 0xff, sizeof(pixels));
	return 0;
}
static void wsdisplay_cnattach(void *s, void *r, int a, int b, long d)
{
	(void)s;
	(void)r;
	(void)a;
	(void)b;
	(void)d;
}
static void genfb_cnattach(void)
{
}
static void config_found(device_t d, void *a, int p, int f)
{
	(void)d;
	(void)a;
	(void)p;
	(void)f;
	attaches++;
}
#define wsemuldisplaydevprint 0
static void genfb_ioctl(void)
{
}
static void genfb_mmap(void)
{
}
static void genfb_pollc(void)
{
}
static void genfb_damage_putchar(void)
{
}
static void genfb_damage_cursor(void)
{
}
static void genfb_damage_copycols(void)
{
}
static void genfb_damage_erasecols(void)
{
}
static void genfb_damage_copyrows(void)
{
}
static void genfb_damage_eraserows(void *a, int b, int c, long d)
{
	erase(a, b, c, d);
}
static void simplefb_ioctl(void)
{
}
static void simplefb_mmap(void)
{
}
static bool simplefb_shutdown(device_t d, int f)
{
	(void)d;
	(void)f;
	return true;
}
static void pmf_device_register1(device_t d, void *a, void *b,
				 bool (*c)(device_t, int))
{
	(void)d;
	(void)a;
	(void)b;
	(void)c;
}
static void print_message(const char *f, ...)
{
	(void)f;
}
static void print_device(device_t d, const char *f, ...)
{
	(void)d;
	(void)f;
}
#define aprint_error print_message
#define aprint_normal print_message
#define aprint_naive print_message
#define aprint_normal_dev print_device
#define aprint_verbose_dev print_device
#define pmap_kernel() NULL
static void pmap_extract(void *p, uintptr_t v, paddr_t *a)
{
	(void)p;
	*a = v;
}
#include "genfb.c.h"
static struct genfb_private private_state;
static void genfb_init(struct genfb_softc *s)
{
	struct properties *p = &s->sc_dev->props;
	s->sc_private = &private_state;
	private_state.sc_enable_shadowfb = shadow;
	s->sc_width = p->width;
	s->sc_height = p->height;
	s->sc_depth = p->depth;
	s->sc_stride = p->stride;
	s->sc_fbsize = p->height * p->stride;
	s->sc_fbaddr = (void *)(uintptr_t)p->virtual_address;
	s->sc_fboffset = p->address;
}
#include "simplefb.c.h"
static struct device dev;
static struct simplefb_softc fb;
static void reset(void)
{
	memset(&dev, 0, sizeof(dev));
	memset(&fb, 0, sizeof(fb));
	memset(&private_state, 0, sizeof(private_state));
	for (unsigned i = 0; i < sizeof(pixels); i++)
		pixels[i] = (unsigned char)(i + 1);
	memcpy(original, pixels, sizeof(pixels));
	clears = splashes = replays = attaches = palettes = 0;
	shadow = false;
	boothowto = 0;
	fail_map = false;
	simplefb_console_phandle = -1;
	genfb_softc = NULL;
	uart = (struct consdev){1, noop, noop, noop};
	cn_tab = &uart;
	fb.sc_gen.sc_dev = &dev;
	fb.sc_phandle = 1;
	fb.sc_framebuffer =
	    (struct simplefb_framebuffer){4, 4, 16, 32, 0, 0x1000, 64};
}
int main(void)
{
	bool preserve = false;
#if defined(SPLASHSCREEN) && defined(WSDISPLAY_EARLYFB_PRESERVE_SPLASH) &&     \
    !defined(WSDISPLAY_MULTICONS)
	preserve = true;
#endif
	reset();
	CHECK(cn_tab == &uart && simplefb_console_phandle == -1);
	CHECK(simplefb_attach_genfb(&fb) == 0);
	CHECK(attaches == 1);
	if (preserve) {
		CHECK(clears == 0 && splashes == 0 && replays == 0 &&
		      palettes == 0);
		CHECK(memcmp(pixels, original, sizeof(pixels)) == 0);
		CHECK(private_state.sc_console_screen.disabled);
	} else {
		CHECK(clears > 0);
#ifdef SPLASHSCREEN
		CHECK(splashes == 1 && palettes == 1);
#endif
	}
	reset();
	shadow = true;
	CHECK(simplefb_attach_genfb(&fb) == 0);
	if (preserve) {
		CHECK(memcmp(pixels, original, sizeof(pixels)) == 0);
		CHECK(memcmp(private_state.sc_shadowfb, original,
			     sizeof(pixels)) == 0);
	}
	free(private_state.sc_shadowfb);
	const int flags[] = {RB_SINGLE, RB_USERCONF, RB_ASKNAME, AB_VERBOSE,
			     AB_DEBUG};
	for (unsigned i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
		reset();
		boothowto = flags[i];
		CHECK(simplefb_attach_genfb(&fb) == 0);
		CHECK(clears > 0 && splashes == 0 && replays > 0);
		CHECK(!private_state.sc_console_screen.disabled);
	}
	for (unsigned i = 0; i < 7; i++) {
		reset();
		switch (i) {
		case 0:
			cn_tab = NULL;
			break;
		case 1:
			uart.cn_pri = CN_NULL;
			break;
		case 2:
			uart.cn_putc = NULL;
			break;
		case 3:
			uart.cn_getc = NULL;
			break;
		case 4:
			uart.cn_pollc = NULL;
			break;
		case 5:
			uart.cn_putc = wsdisplay_cnputc;
			break;
		case 6:
			simplefb_console_phandle = 1;
			break;
		}
		CHECK(simplefb_attach_genfb(&fb) == 0);
		CHECK(clears > 0);
#ifdef SPLASHSCREEN
		CHECK(splashes == 1 && palettes == 1);
#endif
	}
	reset();
	fb.sc_framebuffer_error = EINVAL;
	CHECK(simplefb_attach_genfb(&fb) == EINVAL);
	CHECK(attaches == 0 && memcmp(pixels, original, sizeof(pixels)) == 0);
	reset();
	fb.sc_framebuffer.sf_width = 0;
	CHECK(simplefb_attach_genfb(&fb) != 0);
	CHECK(attaches == 0 && memcmp(pixels, original, sizeof(pixels)) == 0);
	reset();
	fail_map = true;
	CHECK(simplefb_attach_genfb(&fb) != 0);
	CHECK(attaches == 0 && memcmp(pixels, original, sizeof(pixels)) == 0);
	printf("late splash: preserve=%d, diagnostic and console fallbacks "
	       "passed\n",
	       preserve);
	return 0;
}
