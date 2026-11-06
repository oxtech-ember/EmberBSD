/* SPDX-License-Identifier: BSD-2-Clause */
/* RTL8821CE actual runtime; hardware stages are synthetic. */
#include "rtw88-test-kernel.h"
#include <dev/ic/rtw8821c_phy.h>
#include <dev/ic/rtw88var.h>
#include <stdbool.h>
#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); \
    abort(); \
} } while (0)
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define mstohz(x) (x)
static int
kpause(const char *s, bool b, int n, void *p)
{
	(void)s;
	(void)b;
	(void)n;
	(void)p;
	return 0;
}
int
rtw8821c_coex_init(struct rtw88_softc *, uint8_t);
#include "../../sys/dev/ic/rtw8821c_runtime.c"
static int stage, fail, stopped, packets, boot_tx, txs, cancel, iqkfail,
    restore_pause, off_error, expected_bt;
static int hci_prepared, rx_bd_armed, early_rx, power_after_setup;
static struct rtw88_softc *active;
const struct rtw88_fw_profile rtw8821c_fw_profile = {0};
void *kmem_zalloc(size_t n, int f)
{
	(void)f;
	return calloc(1, n);
}
void
kmem_free(void *p, size_t n)
{
	(void)n;
	free(p);
}
static int
step(void)
{
	++stage;
	if (stage == cancel)
		active->sc_generation++;
	return stage == fail ? EIO : 0;
}
int
rtw8821c_identify(struct rtw88_softc *s)
{
	(void)s;
	return step();
}
int
rtw8821c_power_pre(struct rtw88_softc *s)
{
	(void)s;
	return step();
}
int
rtw8821c_power_post(struct rtw88_softc *s)
{
	(void)s;
	return step();
}
int
rtw8821c_power(struct rtw88_softc *s, int on)
{
	(void)s;
	if (!on)
		return off_error ? EIO : 0;
	if (s->sc_chip_private != NULL &&
	    ((struct rtw8821c_runtime *)s->sc_chip_private)->identity_ready) {
		/* Power may enable DMA before firmware download begins. */
		CHECK(rx_bd_armed);
		power_after_setup++;
	}
	return step();
}
int
rtw8821c_read_efuse(struct rtw88_softc *s)
{
	(void)s;
	return step();
}
int
rtw8821c_efuse_decode(const uint8_t *d, size_t n, struct rtw8821c_efuse *e)
{
	(void)d;
	(void)n;
	memset(e, 0, sizeof(*e));
	return step();
}
int
rtw8821c_mac_init(struct rtw88_softc *s)
{
	(void)s;
	return step();
}
int
rtw8821c_coex_init(struct rtw88_softc *s, uint8_t bt)
{
	(void)s;
	CHECK(bt == expected_bt);
	return step();
}
int
rtw8821c_phy_pre(struct rtw8821c_phy *p)
{
	(void)p;
	return step();
}
int
rtw8821c_load_tables(struct rtw8821c_phy *p)
{
	(void)p;
	return step();
}
int
rtw8821c_phy_post(struct rtw8821c_phy *p)
{
	p->post_valid = 1;
	return step();
}
int
rtw8821c_set_channel_tuple(struct rtw8821c_phy *p,
			       const struct rtw88_channel_tuple *t)
{
	(void)p;
	(void)t;
	return step();
}
int
rtw8821c_set_tx_power_tuple(struct rtw8821c_phy *p,
				const struct rtw88_channel_tuple *t, unsigned r)
{
	(void)p;
	(void)t;
	(void)r;
	return step();
}
int
rtw8821c_calibrate(struct rtw8821c_phy *p)
{
	(void)p;
	return iqkfail ? EIO : 0;
}
int
rtw8821c_read_rf(struct rtw8821c_phy *p, unsigned a, uint32_t *v)
{
	(void)p;
	(void)a;
	*v = 0;
	return 0;
}
int
rtw8821c_tx_encode(uint8_t *p, size_t n, const struct rtw88_tx_info *i)
{
	(void)p;
	(void)n;
	(void)i;
	txs++;
	return 0;
}
int
rtw8821c_rx_decode(const uint8_t *p, size_t n, struct rtw88_rx_info *i)
{
	(void)p;
	(void)n;
	(void)i;
	return 0;
}
int
rtw88_fw_parse(const uint8_t *a, size_t n, const struct rtw88_fw_profile *p,
		   struct rtw88_fw_info *i)
{
	(void)a;
	(void)n;
	(void)p;
	(void)i;
	return step();
}
int
rtw88_wcpu_fw_download(struct rtw88_softc *s, uint32_t g, const uint8_t *p,
			   size_t n, const struct rtw88_fw_info *i)
{
	uint8_t d[48];
	(void)g;
	(void)p;
	(void)n;
	(void)i;
	/* Firmware may receive before the download routine returns. */
	CHECK(rx_bd_armed);
	early_rx++;
	boot_tx = rtw8821c_tx_desc(s, d, sizeof(d), NULL);
	return step();
}
int
rtw88_wcpu_hw_feature_request(struct rtw88_softc *s, uint32_t g)
{
	(void)s;
	(void)g;
	return step();
}
int
rtw88_wcpu_hw_feature_read(struct rtw88_softc *s, uint32_t g)
{
	(void)s;
	(void)g;
	return step();
}
int
rtw88_wcpu_station(struct rtw88_softc *s, const uint8_t *b, unsigned a,
		       unsigned r, unsigned l, int v)
{
	(void)s;
	(void)b;
	(void)a;
	(void)r;
	(void)l;
	(void)v;
	return 0;
}
int
rtw88_wcpu_edca(struct rtw88_softc *s, const struct rtw88_edca_params p[RTW88_WME_NAC],
		    unsigned x)
{
	(void)s;
	(void)p;
	(void)x;
	return 0;
}
int
rtw88_wcpu_ra(struct rtw88_softc *s, const struct rtw88_ra_config *p)
{
	(void)s;
	(void)p;
	return 0;
}
int
rtw88_wcpu_mailbox_begin(struct rtw88_softc *s, unsigned n, int *v)
{
	(void)s;
	(void)n;
	(void)v;
	return 0;
}
int
rtw88_wcpu_mailbox_end(struct rtw88_softc *s, int e)
{
	(void)s;
	return e;
}
int
rtw88_wcpu_command(struct rtw88_softc *s, uint8_t x, const void *p,
		       size_t n)
{
	(void)s;
	(void)x;
	(void)p;
	(void)n;
	return 0;
}
int
rtw88_fw_packet(struct rtw88_softc *s, const uint8_t *p, size_t n)
{
	(void)s;
	(void)p;
	(void)n;
	return 0;
}
void
rtw88_ra_invalidate(struct rtw88_softc *s)
{
	(void)s;
}
int
rtw88_stop(struct rtw88_softc *s)
{
	stopped++;
	/* Runtime must enter the single teardown on every failed startup. */
	hci_prepared = rx_bd_armed = 0;
	s->sc_started = s->sc_running = s->sc_powered = s->sc_power_dirty =
	    s->sc_dma_master = 0;
	s->sc_bootstrapping = 0;
	return 0;
}
static int
load(void *c, const char *a, const char *b, const uint8_t **p,
		size_t *n)
{
	static const uint8_t fw = 1;
	(void)c;
	CHECK(strcmp(a, "rtw88") == 0);
	CHECK(strcmp(b, "rtw8821c_fw.bin") == 0);
	*p = &fw;
	*n = 1;
	return step();
}
static void
release(void *c)
{
	(void)c;
}
static int
read1(void *c, uint32_t r, uint8_t *v)
{
	(void)c;
	(void)r;
	*v = 0;
	return step();
}
static int
write1(void *c, uint32_t r, uint8_t v)
{
	(void)c;
	(void)r;
	if (v == 0)
		restore_pause++;
	return step();
}
static int
read4(void *c, uint32_t r, uint32_t *v)
{
	(void)c;
	(void)r;
	*v = 0;
	return step();
}
static int
write4(void *c, uint32_t r, uint32_t v)
{
	(void)c;
	(void)r;
	(void)v;
	return step();
}
static int
prepare(struct rtw88_softc *s)
{
	int e;
	(void)s;
	e = step();
	if (e == 0)
		hci_prepared = 1;
	return e;
}
static int
setup(struct rtw88_softc *s)
{
	int e;
	(void)s;
	CHECK(hci_prepared);
	e = step();
	if (e == 0)
		rx_bd_armed = 1;
	return e;
}
static int
start(struct rtw88_softc *s)
{
	int e = step();
	if (!e)
		s->sc_running = 1;
	return e;
}
static int
packet(struct rtw88_softc *s, const uint8_t *p, size_t n)
{
	(void)s;
	CHECK(n == 32);
	CHECK(p[0] == 1 && p[1] == 0xff);
	if (p[2] == 0x0d)
		CHECK(p[4] == 12 && p[10] == 48);
	else {
		CHECK(p[2] == 0x11 && p[4] == 16);
		CHECK(p[9] == 4 && p[11] == 0x11);
	}
	packets++;
	return step();
}
static int
current(void *v)
{
	(void)v;
	return 0;
}
int
main(void)
{
	struct rtw88_softc sc = {0};
	struct rtw88_bus_ops ops = {.firmware_load = load,
				    .firmware_release = release,
				    .read_1 = read1,
				    .write_1 = write1,
				    .read_4 = read4,
				    .write_4 = write4};
	struct rtw88_hci_ops hci = {.prepare = prepare,
				    .setup = setup,
				    .start = start,
				    .fw_packet = packet};
	struct rtw88_channel_tuple tuple = {
	    .primary = 1, .center = 1, .width_mhz = 20};
	struct rtw8821c_runtime *ctx;
	uint8_t out[48];
	int i, e, maxstage;
	active = &sc;
	sc.sc_ops = &ops;
	sc.sc_hci = &hci;
	sc.sc_resources_ready = 1;
	CHECK(rtw8821c_attach(&sc) == 0);
	CHECK(rtw8821c_discover(&sc) == 0);
	ctx = sc.sc_chip_private;
	ctx->phy.efuse.bt_setting = 1;
	expected_bt = 1;
	stage = 0;
	CHECK(rtw8821c_start(&sc, &tuple) == 0);
	maxstage = stage;
	expected_bt = 0;
	CHECK(boot_tx == 0 && packets == 2 && ctx->needs_iqk);
	CHECK(early_rx == 1 && power_after_setup >= 1 && rx_bd_armed);
	CHECK(rtw8821c_tx_desc(&sc, out, sizeof(out), NULL) == EACCES);
	iqkfail = 1;
	CHECK(rtw8821c_prepare_station(&sc, current, NULL) == EIO);
	CHECK(rtw8821c_tx_desc(&sc, out, sizeof(out), NULL) == EACCES);
	iqkfail = 0;
	CHECK(rtw8821c_prepare_station(&sc, current, NULL) == 0);
	CHECK(rtw8821c_tx_desc(&sc, out, sizeof(out), NULL) == 0);
	{
		struct rtw88_channel_tuple next = {
		    .primary = 6, .center = 6, .width_mhz = 20};
		cancel = stage + 2;
		restore_pause = 0;
		CHECK(rtw8821c_channel(&sc, &next) == EINTR);
		CHECK(sc.sc_stop_requested && ctx->needs_iqk &&
		       restore_pause == 0);
		cancel = 0;
	}
	CHECK(rtw88_stop(&sc) == 0);
	CHECK(rtw8821c_detach(&sc) == 0);
	/* Failed discovery power-off must withhold identity and retain dirty
	 * state. */
	memset(&sc, 0, sizeof(sc));
	active = &sc;
	sc.sc_ops = &ops;
	sc.sc_hci = &hci;
	sc.sc_resources_ready = 1;
	stage = fail = cancel = 0;
	CHECK(rtw8821c_attach(&sc) == 0);
	off_error = 1;
	CHECK(rtw8821c_discover(&sc) == EIO);
	ctx = sc.sc_chip_private;
	CHECK(!ctx->identity_ready && sc.sc_power_dirty && sc.sc_power_error);
	off_error = 0;
	sc.sc_power_dirty = sc.sc_power_error = sc.sc_powered = 0;
	CHECK(rtw8821c_detach(&sc) == 0);
	for (i = 1; i <= maxstage; i++) {
		memset(&sc, 0, sizeof(sc));
		active = &sc;
		sc.sc_ops = &ops;
		sc.sc_hci = &hci;
		sc.sc_resources_ready = 1;
		stage = fail = cancel = 0;
		CHECK(rtw8821c_attach(&sc) == 0);
		ctx = sc.sc_chip_private;
		ctx->identity_ready = 1;
		fail = i;
		int stop_before = stopped;
		e = rtw8821c_start(&sc, &tuple);
		CHECK(stopped == stop_before + (i > 2));
		CHECK(!hci_prepared && !rx_bd_armed);
		CHECK(e != 0);
		CHECK(!sc.sc_started && !sc.sc_rf_enabled);
		fail = 0;
		rtw88_stop(&sc);
		CHECK(rtw8821c_detach(&sc) == 0);
	}
	for (i = 1; i <= maxstage; i++) {
		memset(&sc, 0, sizeof(sc));
		active = &sc;
		sc.sc_ops = &ops;
		sc.sc_hci = &hci;
		sc.sc_resources_ready = 1;
		stage = fail = cancel = 0;
		CHECK(rtw8821c_attach(&sc) == 0);
		ctx = sc.sc_chip_private;
		ctx->identity_ready = 1;
		cancel = i;
		int stop_before = stopped;
		e = rtw8821c_start(&sc, &tuple);
		CHECK(stopped == stop_before + (i > 2));
		CHECK(!hci_prepared && !rx_bd_armed);
		if (e != EINTR)
			fprintf(stderr, "cancel stage %d returned %d\n", i, e);
		CHECK(e == EINTR);
		CHECK(!sc.sc_started && !sc.sc_rf_enabled);
		cancel = 0;
		rtw88_stop(&sc);
		CHECK(rtw8821c_detach(&sc) == 0);
	}
	CHECK(stopped > 0 && txs > 0 && restore_pause == 0);
	active = NULL;
	printf("PASS runtime startup=%d failure stages, bootstrap TX, "
	       "operational IQK fence\n",
	       maxstage);
	return 0;
}
