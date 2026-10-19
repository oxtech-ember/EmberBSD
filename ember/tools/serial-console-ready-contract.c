/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual PLCOM attachment/readiness bodies. Kernel services are controlled. */
#include <sys/types.h>
#include <sys/time.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#define WSDISPLAY_BOOTFB_HANDOFF 1
#define NCIXSCMI_ACPI 1
#define KASSERT(x) assert(x)
#define ASSERT_SLEEPABLE() assert(!in_irq && !in_softirq)
#define KERNEL_LOCK(n,l) do { (void)(l); giant += (n); } while (0)
#define KERNEL_UNLOCK_ONE(l) do { (void)(l); assert(giant > 0); giant--; } while (0)
#define KERNEL_LOCKED_P() (giant > 0)
#define ISSET(x,m) ((x) & (m))
#define SET(x,m) ((x) |= (m))
#define NODEV ((dev_t)-1)
#ifndef makedev
#define makedev(m,u) ((dev_t)(((unsigned long)(m) << 16) | (unsigned int)(u)))
#endif
#define __SHIFTIN(v,m) ((v) * ((m) & (0U - (m))))
#define TIOCFLAG_SOFTCAR 1
#define KM_SLEEP 0
#define MUTEX_DEFAULT 0
#define IPL_HIGH 7
#define IPL_SERIAL 5
#define IPL_VM 6
#define SOFTINT_SERIAL 2
#define FDT_INTR_MPSAFE 1
#define VCHR 1
#define PLCOMDIALOUT_MASK 0x80
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))

typedef unsigned int u_int;
typedef unsigned char u_char;
typedef int kmutex_t;
typedef struct { int unused; } pps_info_t;
typedef struct { int mode; } pps_params_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef uint64_t bus_addr_t;
typedef size_t bus_size_t;
typedef struct device *device_t;
typedef struct cfdata *cfdata_t;
struct cfdata { unsigned int cf_flags; };
struct device { void *private; int unit; bool active; struct cfdata cf; };
enum devact { DVACT_DEACTIVATE, DVACT_OTHER };
struct callout { int unused; };
struct tty {
	void (*t_oproc)(struct tty *);
	int (*t_param)(struct tty *, struct termios *);
	int (*t_hwiflow)(struct tty *, int);
	dev_t t_dev;
	tcflag_t t_cflag;
};
struct consdev {
	void (*cn_halt)(dev_t);
	void (*cn_putc)(dev_t, int);
	void (*cn_pollc)(dev_t, int);
	int (*cn_getc)(dev_t);
	dev_t cn_dev;
};
struct cfattach { const char *ca_name; };
struct cfattach plcom_acpi_ca = { "plcom_acpi" };
static struct cfattach *device_cfattach(device_t d) { (void)d; return &plcom_acpi_ca; }
struct wsdisplay_bootfb_lease;
#ifndef SERIAL_READY_LIBRARY
static bool lease_live;
static device_t wsdisplay_bootfb_lease_device(struct wsdisplay_bootfb_lease *l)
{ return l != NULL && lease_live ? (device_t)(uintptr_t)1 : NULL; }
static bool cn_publication_closed(void) { return false; }
#else
extern device_t wsdisplay_bootfb_lease_device(struct wsdisplay_bootfb_lease *);
extern bool cn_publication_closed(void);
#endif
struct cfdriver { int unused; };
struct cdevsw { int unused; };
#include "plcomvar.inc"
#include "plcomreg.inc"
#include "plcom-console-api.inc"

static unsigned int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "CHECK line %u: %s\n", (unsigned int)__LINE__, #x); \
	return 1; } } while (0)
#define MODEL_CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "CHECK line %u: %s\n", (unsigned int)__LINE__, #x); \
	exit(1); } } while (0)
static void lifetime_effect(void);
#ifdef SERIAL_READY_LIBRARY
extern int *serial_test_giant(void);
extern bool *serial_test_cold(void), *serial_test_irq(void), *serial_test_softirq(void);
extern const char **serial_test_panic(void);
#define giant (*serial_test_giant())
#define cold (*serial_test_cold())
#define in_irq (*serial_test_irq())
#define in_softirq (*serial_test_softirq())
#define panicstr (*serial_test_panic())
#else
static int giant, cold, in_irq, in_softirq;
static const char *panicstr;
#endif
static int major_number = 12;
static unsigned int writes, maps, hard_calls, soft_calls, attached, freed;
static unsigned int sequence, tty_step, soft_step, hard_step, publish_step;
static bool fail_map, fail_hard, fail_soft, foreign_lookup;
static int reinit_error;
static struct device device, rootdev;
static struct plcom_softc *live_sc;
static struct tty tty_storage, foreign_tty;
static unsigned char ring[8192];
static int hard_cookie, soft_cookie, bus_tag, alternate_tag;
struct cfdriver plcom_cd;
static const struct cdevsw plcom_cdevsw;
struct consdev plcomcons = { .cn_dev = NODEV };
static struct consdev foreign_cons = { .cn_dev = 77 }, *cn_tab;
static int timecounter_lock;
static u_int plcom_rbuf_size = PLCOM_RING_SIZE;
struct cnm_state { int unused; };
static int cpu_intr_p(void) { return in_irq; }
static int cpu_softintr_p(void) { return in_softirq; }
static void *device_private(device_t d) { return d->private; }
static int device_unit(device_t d) { return d->unit; }
static bool device_is_active(device_t d) { return d->active; }
static void *device_lookup_private(struct cfdriver *cd, int unit)
{
	assert(cd == &plcom_cd);
	return !foreign_lookup && unit == device.unit ? live_sc : NULL;
}
static int cdevsw_lookup_major(const struct cdevsw *cd)
{
	assert(cd == &plcom_cdevsw); return major_number;
}
static bool bus_space_is_equal(bus_space_tag_t a, bus_space_tag_t b) { return a == b; }
static void plcomstart(struct tty *tp) { (void)tp; }
static int plcomparam(struct tty *tp, struct termios *t) { (void)tp; (void)t; return 0; }
static int plcomhwiflow(struct tty *tp, int v) { (void)tp; return v; }
static void callout_init(struct callout *c, int flags) { lifetime_effect(); (void)c; (void)flags; }
static void callout_destroy(struct callout *c) { (void)c; }
static void mutex_init(kmutex_t *m, int t, int p) { (void)m; (void)t; (void)p; }
static void mutex_destroy(kmutex_t *m) { (void)m; }
static void mutex_spin_enter(kmutex_t *m) { (void)m; }
static void mutex_spin_exit(kmutex_t *m) { (void)m; }
static void delay(unsigned int n) { (void)n; }
static const char *device_xname(device_t d) { (void)d; return "plcom0"; }
static void diagnostic_model(const char *format, ...)
{
	char buffer[512];
	va_list ap;

	va_start(ap, format);
	(void)vsnprintf(buffer, sizeof(buffer), format, ap);
	va_end(ap);
}
#define aprint_error_dev(d,...) diagnostic_model(__VA_ARGS__)
#define aprint_normal_dev(d,...) diagnostic_model(__VA_ARGS__)
#define aprint_verbose_dev(d,...) diagnostic_model(__VA_ARGS__)
#define aprint_debug_dev(d,...) diagnostic_model(__VA_ARGS__)
#define aprint_error(...) diagnostic_model(__VA_ARGS__)
#define aprint_normal(...) diagnostic_model(__VA_ARGS__)
#define aprint_naive(...) diagnostic_model(__VA_ARGS__)
static uint32_t pread_model(struct plcom_instance *p, unsigned int offset)
{
	(void)p; (void)offset; return 0;
}
static void pwrite_model(struct plcom_instance *p, unsigned int offset, uint32_t value)
{
	(void)p; (void)offset; (void)value; writes++;
}
#define PREAD1(p,o) pread_model(p,o)
#define PWRITE1(p,o,v) pwrite_model(p,o,v)
#define PWRITE2(p,o,v) pwrite_model(p,o,v)
#define PWRITE4(p,o,v) pwrite_model(p,o,v)
static struct tty *tty_alloc(void) { memset(&tty_storage, 0, sizeof(tty_storage)); return &tty_storage; }
static void tty_attach(struct tty *tp) { assert(tp == &tty_storage); attached++; tty_step = ++sequence; }
static void tty_detach(struct tty *tp) { (void)tp; }
static void tty_free(struct tty *tp) { lifetime_effect(); assert(tp == &tty_storage); freed++; }
static void *kmem_alloc(size_t n, int flags) { assert(n <= sizeof(ring)); (void)flags; return ring; }
static void kmem_free(void *p, size_t n) { lifetime_effect(); assert(p == ring); (void)n; }
static void plcomsoft(void *p) { (void)p; }
static void *softint_establish(int flags, void (*fn)(void *), void *arg)
{
	assert(flags == SOFTINT_SERIAL && fn == plcomsoft && arg == live_sc);
	soft_calls++; soft_step = ++sequence; return fail_soft ? NULL : &soft_cookie;
}
static void softint_disestablish(void *p) { (void)p; }
static void plcom_enable_debugport(struct plcom_softc *sc) { (void)sc; writes++; }
static void vdevgone(int m, int a, int b, int t) { (void)m; (void)a; (void)b; (void)t; }
static void plcom_hwiflow(struct plcom_softc *sc) { (void)sc; }
static void plcom_break(struct plcom_softc *sc, int v) { (void)sc; (void)v; }
static void plcom_modem(struct plcom_softc *sc, int v) { (void)sc; (void)v; }
static void microuptime(struct timeval *t) { memset(t, 0, sizeof(*t)); }
static int plcominit(struct plcom_instance *p, int r, int f, tcflag_t c)
{ lifetime_effect(); (void)p; (void)r; (void)f; (void)c; writes++; return reinit_error; }
static void plcom_copy_attach(struct plcom_instance *p) { (void)p; }
static void cn_init_magic(struct cnm_state *s) { (void)s; }
static void cn_set_magic(const char *s) { (void)s; }
static void bus_space_unmap(bus_space_tag_t t, bus_space_handle_t h, bus_size_t n)
{ lifetime_effect(); (void)t; (void)h; (void)n; writes++; }
static int bus_space_map(bus_space_tag_t t, bus_addr_t a, bus_size_t n, int f,
    bus_space_handle_t *out)
{
	(void)t; (void)a; (void)n; (void)f; maps++;
	if (fail_map) return EIO;
	*out = 99; return 0;
}
int plcomintr(void *p) { (void)p; return 0; }

#include "provider.inc"
static void lifetime_effect(void)
{ MODEL_CHECK(!plcom_console_ready.published || plcom_console_ready.revoked); }
static void disable_model(struct plcom_softc *sc) { (void)sc; lifetime_effect(); }
static void plcom_config(struct plcom_softc *);
#include "common.inc"

typedef void *ACPI_HANDLE;
typedef uint64_t ACPI_INTEGER;
typedef int ACPI_STATUS;
#define ACPI_SUCCESS(s) ((s) == 0)
#define ACPI_FAILURE(s) ((s) != 0)
#define PLCOM_ACPI_BCM2837 1
#define PLCOM_ACPI_GENERIC 2
struct acpi_devnode { device_t ad_device, ad_root; ACPI_HANDLE ad_handle; };
struct acpi_attach_args { struct acpi_devnode *aa_node; bus_space_tag_t aa_memt; };
struct acpi_resources { int unused; };
struct acpi_mem { bus_addr_t ar_base; bus_size_t ar_length; };
struct acpi_irq { int unused; };
struct plcom_acpi_softc {
	struct plcom_softc pa_sc;
	struct acpi_attach_args pa_aa;
	device_t pa_root;
	ACPI_HANDLE pa_provider;
	uint32_t pa_clock_id;
	bus_size_t pa_span;
	bool pa_once;
};
struct device_compatible_entry { int value; };
static struct device_compatible_entry compatible, compat_data[1];
static struct acpi_devnode acpi_node;
static struct acpi_mem acpi_memory = { 4096, 4096 };
static struct acpi_irq acpi_irq;
static int acpi_resource_parse_ops_default, provider_identity;
static int acpi_resource_parse(device_t d, ACPI_HANDLE h, const char *s,
    struct acpi_resources *r, void *ops)
{ (void)d; (void)h; (void)s; (void)r; (void)ops; return 0; }
static struct acpi_mem *acpi_res_mem(struct acpi_resources *r, int i) { (void)r; (void)i; return &acpi_memory; }
static struct acpi_irq *acpi_res_irq(struct acpi_resources *r, int i) { (void)r; (void)i; return &acpi_irq; }
static void acpi_resource_cleanup(struct acpi_resources *r) { (void)r; }
static const struct device_compatible_entry *acpi_compatible_lookup(
    const struct acpi_attach_args *aa, const struct device_compatible_entry *e)
{ (void)aa; (void)e; return &compatible; }
static int plcom_acpi_cix(device_t d, const struct acpi_attach_args *aa,
    const struct acpi_resources *r, const struct acpi_mem *m,
    const struct acpi_irq *i, bool *selected)
{ (void)d; (void)aa; (void)r; (void)m; (void)i; *selected = false; return 0; }
static int acpi_dsd_integer(ACPI_HANDLE h, const char *s, ACPI_INTEGER *out)
{ (void)h; (void)s; (void)out; return ENOENT; }
static void *acpi_intr_establish(device_t d, uint64_t h, int ipl, bool safe,
    int (*fn)(void *), void *arg, const char *name)
{
	assert(d == &device && fn == plcomintr && arg == live_sc && safe);
	(void)h; (void)ipl; (void)name;
	hard_calls++; hard_step = ++sequence; return fail_hard ? NULL : &hard_cookie;
}
static int plcom_acpi_provider(struct acpi_devnode *node, ACPI_HANDLE *out)
{ (void)node; *out = &provider_identity; return 0; }
static int cixscmi_acpi_uart_rate(ACPI_HANDLE h, uint32_t id, uint64_t *out)
{ (void)h; (void)id; *out = 100000000; return 0; }
#include "acpi.inc"

struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
struct clk { int unused; };
static struct clk clock_model;
static int fdtbus_get_reg(int node, int index, bus_addr_t *addr, bus_size_t *size)
{ (void)node; (void)index; *addr = 4096; *size = 4096; return 0; }
static bool fdtbus_intr_str(int p, int i, char *out, size_t n)
{ (void)p; (void)i; if (n) *out = 0; return true; }
static struct clk *fdtbus_clock_get_index(int p, int i) { (void)p; return i == 0 ? &clock_model : NULL; }
static int clk_enable(struct clk *c) { (void)c; return 0; }
static unsigned int clk_get_rate(struct clk *c) { (void)c; return 100000000; }
static const void *fdtbus_get_prop(int p, const char *name, int *len)
{ (void)p; (void)name; (void)len; return NULL; }
static const struct device_compatible_entry *of_compatible_lookup(int p,
    const struct device_compatible_entry *e) { (void)p; return e; }
static void *fdtbus_intr_establish_xname(int p, int i, int ipl, int flags,
    int (*fn)(void *), void *arg, const char *name)
{ (void)p; (void)i; (void)flags; return acpi_intr_establish(&device, 0, ipl, true, fn, arg, name); }
#define be32toh(x) (x)
#include "fdt.inc"

static struct plcom_acpi_softc acpi_sc;
static void reset(void)
{
	memset(&acpi_sc, 0, sizeof(acpi_sc));
	memset(&device, 0, sizeof(device));
	memset(&plcom_console_ready, 0, sizeof(plcom_console_ready));
	memset(&plcomcons_info, 0, sizeof(plcomcons_info));
	live_sc = &acpi_sc.pa_sc;
	device.private = live_sc; device.active = true;
	live_sc->sc_dev = &device;
	plcomcons_info.pi_iot = &bus_tag;
	plcomcons_info.pi_iobase = 4096;
	plcomcons_info.pi_type = PLCOM_TYPE_PL011;
	plcomconsattached = 0; plcomcons.cn_dev = NODEV;
	cn_tab = &foreign_cons;
	giant = 1; cold = in_irq = in_softirq = 0; panicstr = NULL;
	writes = maps = hard_calls = soft_calls = attached = freed = 0;
	sequence = tty_step = soft_step = hard_step = publish_step = 0;
	fail_map = fail_hard = fail_soft = foreign_lookup = false;
	major_number = 12; reinit_error = 0;
	compatible.value = 0; compat_data[0].value = PLCOM_TYPE_PL011;
	acpi_node.ad_device = &device; acpi_node.ad_root = &rootdev;
	acpi_node.ad_handle = &acpi_node;
	acpi_sc.pa_aa.aa_node = &acpi_node;
	acpi_sc.pa_aa.aa_memt = &bus_tag;
	acpi_sc.pa_root = &rootdev;
	acpi_sc.pa_provider = &provider_identity;
	acpi_sc.pa_span = 4096;
}
static void attach_path(unsigned int path)
{
	if (path == 0) {
		plcom_acpi_attach(&rootdev, &device, &acpi_sc.pa_aa);
	} else if (path == 1) {
		live_sc->sc_pi.pi_iot = &bus_tag;
		live_sc->sc_pi.pi_iobase = 4096;
		live_sc->sc_pi.pi_type = PLCOM_TYPE_PL011;
		plcom_acpi_cix_deferred(&device);
	} else {
		struct fdt_attach_args args = { 1, &bus_tag };
		plcom_fdt_attach(&rootdev, &device, &args);
	}
	publish_step = sequence;
}
static int absent(void)
{
	const struct plcom_console_ready *cap = (const void *)(uintptr_t)1;
	int error = plcom_console_ready_get(&cap);
	CHECK(error != 0 && cap == (const void *)(uintptr_t)1);
	return 0;
}
static int tests(void)
{
	const struct plcom_console_ready *cap, *other;
	dev_t dev, canary = (dev_t)37;
	for (unsigned int path = 0; path < 3; path++) {
		reset(); CHECK(absent() == 0); attach_path(path);
		CHECK(attached == 1 && soft_calls == 1 && hard_calls == 1);
		CHECK(tty_step < soft_step && soft_step < hard_step && hard_step <= publish_step);
		CHECK(plcom_console_ready_get(&cap) == 0);
		CHECK(plcom_console_ready_validate(cap, &plcomcons, &dev) == 0);
		CHECK(dev == makedev(12, 0));
		unsigned int before = writes, before_maps = maps;
		CHECK(plcom_console_ready_get(&other) == 0 && other == cap);
		CHECK(plcom_console_ready_validate(cap, &plcomcons, &dev) == 0);
		CHECK(writes == before && maps == before_maps && cn_tab == &foreign_cons);
		struct plcom_console_ready counterfeit = plcom_console_ready;
		CHECK(plcom_console_ready_validate(&counterfeit, &plcomcons, &canary) == EPERM && canary == 37);
		CHECK(plcom_console_ready_validate((const void *)(uintptr_t)1, &plcomcons, &canary) == EPERM && canary == 37);
		CHECK(plcom_console_ready_validate(cap, &foreign_cons, &canary) == EPERM && canary == 37);
		CHECK(plcom_console_ready_get(NULL) == EINVAL);
		CHECK(plcom_console_ready_validate(cap, &plcomcons, NULL) == EINVAL);
		for (unsigned int failure = 0; failure < 3; failure++) {
			reset(); fail_map = failure == 0; fail_soft = failure == 1; fail_hard = failure == 2;
			attach_path(path); CHECK(absent() == 0);
			if (path == 0 && fail_soft) CHECK(hard_calls == 0);
		}
	}
	reset();
	live_sc->sc_pi = plcomcons_info;
	plcom_attach_subr(live_sc);
	plcom_console_attach_complete(live_sc, NULL);
	CHECK(!plcom_console_ready.published && absent() == 0);
	for (unsigned int failure = 0; failure < 17; failure++) {
		reset(); attach_path(0); CHECK(plcom_console_ready_get(&cap) == 0);
		switch (failure) {
		case 0: live_sc->enabled = 0; break;
		case 1: device.active = false; break;
		case 2: live_sc->sc_hwflags &= ~PLCOM_HW_DEV_OK; break;
		case 3: live_sc->sc_hwflags |= PLCOM_HW_KGDB; break;
		case 4: live_sc->sc_tty = &foreign_tty; break;
		case 5: live_sc->sc_si = &hard_cookie; break;
		case 6: live_sc->sc_rbuf = NULL; break;
		case 7: tty_storage.t_dev++; break;
		case 8: plcomcons.cn_dev++; break;
		case 9: foreign_lookup = true; break;
		case 10: major_number = -1; break;
		case 11: plcomcons_info.pi_iot = &alternate_tag; break;
		case 12: tty_storage.t_oproc = NULL; break;
		case 13: tty_storage.t_param = NULL; break;
		case 14: tty_storage.t_hwiflow = NULL; break;
		case 15: live_sc->sc_hwflags &= ~PLCOM_HW_CONSOLE; break;
		case 16: device.private = &foreign_tty; break;
		}
		CHECK(plcom_console_ready_validate(cap, &plcomcons, &canary) != 0 && canary == 37);
	}
	for (unsigned int context = 0; context < 5; context++) {
		reset(); attach_path(0); CHECK(plcom_console_ready_get(&cap) == 0);
		if (context == 0) cold = 1;
		if (context == 1) in_irq = 1;
		if (context == 2) in_softirq = 1;
		if (context == 3) panicstr = "panic";
		if (context == 4) giant = 0;
		CHECK(plcom_console_ready_validate(cap, &plcomcons, &canary) == (context == 4 ? EDEADLK : EWOULDBLOCK) && canary == 37);
	}
	for (unsigned int mutation = 0; mutation < 6; mutation++) {
		reset(); attach_path(0); CHECK(plcom_console_ready_get(&cap) == 0);
		struct plcom_instance pi = plcomcons_info;
		if (mutation == 0) CHECK(plcom_activate(&device, DVACT_DEACTIVATE) == 0);
		if (mutation == 1) plcomcndetach();
		if (mutation == 2) CHECK(plcomcnattach(&pi, 115200, 100000000, 0, -1) == 0);
		if (mutation == 3) plcom_attach_subr(live_sc);
		if (mutation == 4) { live_sc->disable = disable_model; plcom_shutdown(live_sc); }
		if (mutation == 5) { live_sc->sc_hwflags &= ~PLCOM_HW_CONSOLE; CHECK(plcom_detach(&device, 0) == 0); }
		CHECK(plcom_console_ready.revoked);
		live_sc->enabled = 1; plcomcons_info = pi;
		plcom_console_attach_complete(live_sc, &hard_cookie);
		CHECK(plcom_console_ready_validate(cap, &plcomcons, &canary) == ENXIO && canary == 37);
		CHECK(absent() == 0 && giant == 1);
	}
	reset(); attach_path(0); CHECK(plcom_console_ready_get(&cap) == 0);
	CHECK(plcom_detach(&device, 0) == EBUSY && !plcom_console_ready.revoked);
	CHECK(plcom_console_ready_validate(cap, &plcomcons, &dev) == 0);
	CHECK(plcom_activate(&device, DVACT_OTHER) == EOPNOTSUPP);
	CHECK(!plcom_console_ready.revoked);
	return 0;
}
#ifdef SERIAL_READY_LIBRARY
extern void serial_test_putc(dev_t, int), serial_test_pollc(dev_t, int);
extern int serial_test_getc(dev_t);
struct consdev *serial_fixture_setup(void)
{
	reset(); attach_path(0);
	plcomcons.cn_putc = serial_test_putc;
	plcomcons.cn_getc = serial_test_getc;
	plcomcons.cn_pollc = serial_test_pollc;
	return &plcomcons;
}
void serial_fixture_held_mutations(void)
{
	struct plcom_instance pi = plcomcons_info;
	unsigned before = writes, before_freed = freed, before_maps = maps, before_irq = hard_calls;
	MODEL_CHECK(plcom_console_ready.held_lease != NULL);
	plcom_acpi_attach(&rootdev, &device, &acpi_sc.pa_aa);
	MODEL_CHECK(maps == before_maps && hard_calls == before_irq && writes == before);
	MODEL_CHECK(plcom_activate(&device, DVACT_DEACTIVATE) == EBUSY);
	plcomcndetach();
	MODEL_CHECK(plcomcnattach(&pi, 115200, 100000000, 0, -1) == EBUSY);
	plcom_attach_subr(live_sc);
	struct plcom_softc replacement = *live_sc;
	replacement.sc_dev = &rootdev; rootdev.private = &replacement;
	plcom_attach_subr(&replacement);
	plcom_acpi_attach(NULL, &rootdev, &acpi_sc.pa_aa);
	MODEL_CHECK(maps == before_maps && hard_calls == before_irq && writes == before);
	MODEL_CHECK(writes == before && freed == before_freed);
	live_sc->disable = disable_model; plcom_shutdown(live_sc);
	MODEL_CHECK(live_sc->enabled && !plcom_console_ready.revoked);
	MODEL_CHECK(plcom_console_ready.held_lease != NULL);
}
void serial_fixture_revoke(void) { MODEL_CHECK(plcom_console_ready_revoke(NULL)); }
bool serial_fixture_held(void) { return plcom_console_ready.held_lease != NULL; }
/* Fault injection bypasses the production release refusal after CLOSED. */
void serial_fixture_drop_hold(void) { plcom_console_ready.held_lease = NULL; }
#endif
int main(void)
{
	int error = tests();
	(void)plcom_shutdown;
	(void)device_is_active;
	(void)tty_attach;
	if (error == 0) printf("serial console ready: %u checks PASS\n", checks);
	return error;
}
