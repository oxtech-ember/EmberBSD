/*
 * Run the production NVMe composite temperature sensor functions from
 * sys/dev/ic/nvme.c against a fake admin queue, DMA buffer and sysmon.
 * The shell driver ember/tests/nvme-temperature.sh extracts the functions
 * and the log page constants.  AI-assisted (Claude Code, Claude Opus 5.5).
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvme-temperature-defs.h"

#define BUS_DMASYNC_PREREAD	1
#define BUS_DMASYNC_POSTREAD	2

#define ENVSYS_STEMP		1
#define ENVSYS_SVALID		1
#define ENVSYS_SINVALID		2
#define ENVSYS_FMONLIMITS	0x10
#define PROP_CRITMAX		0x0001
#define PROP_WARNMAX		0x0004
#define PROP_OTHER		0x0100

typedef struct fake_device *device_t;
struct fake_device { const char *xname; };

typedef struct {
	int		units;
	int		state;
	int		flags;
	int32_t		value_cur;
	char		desc[32];
} envsys_data_t;

typedef struct {
	int32_t		sel_critmax;
	int32_t		sel_warnmax;
} sysmon_envsys_lim_t;

struct sysmon_envsys {
	const char	*sme_name;
	void		*sme_cookie;
	void		(*sme_refresh)(struct sysmon_envsys *, envsys_data_t *);
	void		(*sme_get_limits)(struct sysmon_envsys *,
			    envsys_data_t *, sysmon_envsys_lim_t *, uint32_t *);
	envsys_data_t	*sensor;
	bool		registered;
};

struct nvme_sqe {
	uint8_t		opcode;
	uint32_t	nsid;
	struct { uint64_t prp[2]; } entry;
	uint32_t	cdw10;
};
struct nvme_cqe { uint16_t flags; };
struct nvme_queue { int unused; };
struct nvme_ccb {
	void		(*ccb_done)(struct nvme_queue *, struct nvme_ccb *,
			    struct nvme_cqe *);
	void		*ccb_cookie;
};
struct nvme_dmamem {
	uint8_t		kva[NVM_LOG_PAGE_SMART_HEALTH_LEN];
	uint64_t	dva;
	size_t		size;
};
struct nvm_identify_controller { uint16_t wctemp, cctemp; };
struct nvme_softc {
	device_t	sc_dev;
	struct nvme_queue *sc_admin_q;
	struct nvm_identify_controller sc_identify;
	struct sysmon_envsys *sc_sme;
	envsys_data_t	sc_temp;
	struct nvme_dmamem *sc_smart;
	bool		sc_temp_suspended;
	bool		sc_temp_dead;
};

#define NVME_DMA_DVA(m)		((m)->dva)
#define NVME_DMA_KVA(m)		((void *)(m)->kva)
#define htolem32(p, v)		(*(p) = (uint32_t)(v))
#define htolem64(p, v)		(*(p) = (uint64_t)(v))

#undef le16dec
#define le16dec fake_le16dec
static uint16_t
le16dec(const void *p)
{
	const uint8_t *b = p;

	return (uint16_t)(b[0] | b[1] << 8);
}

static size_t
fake_strlcpy(char *dst, const char *src, size_t n)
{
	size_t len = strlen(src);

	if (n > 0) {
		size_t c = len < n - 1 ? len : n - 1;
		memcpy(dst, src, c);
		dst[c] = '\0';
	}
	return len;
}
#undef strlcpy
#define strlcpy fake_strlcpy

/* Fake state. */
static struct {
	bool		no_ccb, no_mem, attach_fails, register_fails;
	int		ccb_get, ccb_put, polls, last_timo, errors;
	bool		last_wait, prepared;
	int		syncs[8], nsyncs;
	int		poll_rv;
	uint8_t		page[NVM_LOG_PAGE_SMART_HEALTH_LEN];
	struct nvme_sqe	last_sqe;
	int		allocs, frees, creates, destroys, unregisters;
	struct nvme_ccb	ccb;
	struct nvme_dmamem mem;
	struct sysmon_envsys sme;
} fk;

static void
fake_reset(void)
{
	memset(&fk, 0, sizeof(fk));
	fk.mem.dva = 0x123456789000ULL;
}

static struct nvme_ccb *
nvme_ccb_get(struct nvme_queue *q, bool wait)
{
	(void)q;
	fk.ccb_get++;
	fk.last_wait = wait;
	return fk.no_ccb ? NULL : &fk.ccb;
}

static void
nvme_ccb_put(struct nvme_queue *q, struct nvme_ccb *ccb)
{
	(void)q; (void)ccb;
	fk.ccb_put++;
}

static void
nvme_dmamem_sync(struct nvme_softc *sc, struct nvme_dmamem *m, int op)
{
	(void)sc; (void)m;
	if (fk.nsyncs < 8)
		fk.syncs[fk.nsyncs++] = op;
	fk.prepared = op == BUS_DMASYNC_PREREAD;
}

static void
nvme_empty_done(struct nvme_queue *q, struct nvme_ccb *ccb,
    struct nvme_cqe *cqe)
{
	(void)q; (void)ccb; (void)cqe;
}

static void
nvme_sqe_fill(struct nvme_queue *q, struct nvme_ccb *ccb, void *slot)
{
	(void)q;
	*(struct nvme_sqe *)slot = *(struct nvme_sqe *)ccb->ccb_cookie;
}

static int
nvme_poll(struct nvme_softc *sc, struct nvme_queue *q, struct nvme_ccb *ccb,
    void (*fill)(struct nvme_queue *, struct nvme_ccb *, void *), int timo)
{
	(void)sc;
	fk.polls++;
	fk.last_timo = timo;
	(*fill)(q, ccb, &fk.last_sqe);
	/* The device writes the page only after the pre-read sync. */
	if (fk.prepared)
		memcpy(fk.mem.kva, fk.page, sizeof(fk.page));
	return fk.poll_rv;
}

static struct nvme_dmamem *
nvme_dmamem_alloc(struct nvme_softc *sc, size_t size)
{
	(void)sc;
	if (fk.no_mem)
		return NULL;
	fk.allocs++;
	fk.mem.size = size;
	return &fk.mem;
}

static void
nvme_dmamem_free(struct nvme_softc *sc, struct nvme_dmamem *m)
{
	(void)sc; (void)m;
	fk.frees++;
}

static struct sysmon_envsys *
sysmon_envsys_create(void)
{
	fk.creates++;
	memset(&fk.sme, 0, sizeof(fk.sme));
	return &fk.sme;
}

static void
sysmon_envsys_destroy(struct sysmon_envsys *sme)
{
	(void)sme;
	fk.destroys++;
}

static int
sysmon_envsys_sensor_attach(struct sysmon_envsys *sme, envsys_data_t *e)
{
	if (fk.attach_fails)
		return EINVAL;
	sme->sensor = e;
	return 0;
}

static int
sysmon_envsys_register(struct sysmon_envsys *sme)
{
	if (fk.register_fails)
		return EEXIST;
	sme->registered = true;
	return 0;
}

static void
sysmon_envsys_unregister(struct sysmon_envsys *sme)
{
	sme->registered = false;
	fk.unregisters++;
}

static const char *
device_xname(device_t d)
{
	return d->xname;
}

#define aprint_error_dev(dev, ...)	((void)(dev), fk.errors++)

static void	nvme_sensors_attach(struct nvme_softc *);
static void	nvme_sensors_detach(struct nvme_softc *);
static int	nvme_smart_temperature(struct nvme_softc *, uint32_t *);
static void	nvme_sensors_refresh(struct sysmon_envsys *, envsys_data_t *);
static void	nvme_sensors_get_limits(struct sysmon_envsys *, envsys_data_t *,
		    sysmon_envsys_lim_t *, uint32_t *);

#include "nvme-temperature.h"

static int checks, failures;

#define CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
	}								\
} while (0)

static struct fake_device dev = { "nvme0" };
static struct nvme_queue adminq;

static void
softc(struct nvme_softc *sc)
{
	memset(sc, 0, sizeof(*sc));
	sc->sc_dev = &dev;
	sc->sc_admin_q = &adminq;
	sc->sc_smart = &fk.mem;
}

static envsys_data_t
refresh(struct nvme_softc *sc, unsigned kelvin, int poll_rv)
{
	struct sysmon_envsys sme = { .sme_cookie = sc };
	envsys_data_t e = { .state = ENVSYS_SVALID, .value_cur = -7 };

	fk.page[NVM_SMART_COMPOSITE_TEMP] = kelvin & 0xff;
	fk.page[NVM_SMART_COMPOSITE_TEMP + 1] = (kelvin >> 8) & 0xff;
	fk.poll_rv = poll_rv;
	nvme_sensors_refresh(&sme, &e);
	return e;
}

static void
check_command(void)
{
	struct nvme_softc sc;
	envsys_data_t e;

	fake_reset();
	softc(&sc);
	e = refresh(&sc, 324, 0);
	CHECK(fk.polls == 1);
	CHECK(fk.ccb_get == 1 && fk.ccb_put == 1);
	CHECK(!fk.last_wait);
	CHECK(fk.last_sqe.opcode == NVM_ADMIN_GET_LOG_PG);
	CHECK(fk.last_sqe.nsid == 0xffffffffU);
	CHECK(fk.last_sqe.entry.prp[0] == 0x123456789000ULL);
	CHECK((fk.last_sqe.cdw10 & 0xff) == NVM_LOG_PAGE_SMART_HEALTH);
	CHECK(((fk.last_sqe.cdw10 >> 16) + 1) * 4 ==
	    NVM_LOG_PAGE_SMART_HEALTH_LEN);
	CHECK(fk.last_sqe.cdw10 == 0x007f0002U);
	CHECK(fk.last_timo == NVME_TIMO_LOG && NVME_TIMO_LOG > 0);
	CHECK(fk.nsyncs == 2);
	CHECK(fk.syncs[0] == BUS_DMASYNC_PREREAD);
	CHECK(fk.syncs[1] == BUS_DMASYNC_POSTREAD);
	CHECK(e.state == ENVSYS_SVALID);
	CHECK(e.value_cur == 324000000);
}

static void
check_readings(void)
{
	struct nvme_softc sc;
	envsys_data_t e;

	fake_reset();
	softc(&sc);
	e = refresh(&sc, 1000, 0);
	CHECK(e.state == ENVSYS_SVALID && e.value_cur == 1000000000);
	e = refresh(&sc, 1, 0);
	CHECK(e.state == ENVSYS_SVALID && e.value_cur == 1000000);
	e = refresh(&sc, 0, 0);
	CHECK(e.state == ENVSYS_SINVALID);
	e = refresh(&sc, 1001, 0);
	CHECK(e.state == ENVSYS_SINVALID);
	e = refresh(&sc, 0xffff, 0);
	CHECK(e.state == ENVSYS_SINVALID);
	/* A completion status (even) is an error but the slot is reused. */
	e = refresh(&sc, 330, 0x4 << 1);
	CHECK(e.state == ENVSYS_SINVALID);
	CHECK(fk.ccb_get == fk.ccb_put && !sc.sc_temp_dead);

	/* No free command slot: no wait, no command, invalid. */
	fake_reset();
	softc(&sc);
	fk.no_ccb = true;
	e = refresh(&sc, 330, 0);
	CHECK(e.state == ENVSYS_SINVALID);
	CHECK(fk.ccb_get == 1 && !fk.last_wait);
	CHECK(fk.polls == 0 && fk.ccb_put == 0 && fk.nsyncs == 0);

	/* Suspended controller: no command at all, then normal again. */
	fake_reset();
	softc(&sc);
	sc.sc_temp_suspended = true;
	e = refresh(&sc, 330, 0);
	CHECK(e.state == ENVSYS_SINVALID && fk.ccb_get == 0 && fk.polls == 0);
	sc.sc_temp_suspended = false;
	e = refresh(&sc, 330, 0);
	CHECK(e.state == ENVSYS_SVALID && e.value_cur == 330000000);

	/*
	 * Timeout (nvme_poll returns 1): the slot is retired with a no-op
	 * completion, the sensor stops after one message.
	 */
	fake_reset();
	softc(&sc);
	e = refresh(&sc, 330, 1);
	CHECK(e.state == ENVSYS_SINVALID);
	CHECK(fk.ccb_get == 1 && fk.ccb_put == 0);
	CHECK(fk.ccb.ccb_done == nvme_empty_done);
	CHECK(sc.sc_temp_dead && fk.errors == 1);
	e = refresh(&sc, 330, 0);
	CHECK(e.state == ENVSYS_SINVALID);
	CHECK(fk.ccb_get == 1 && fk.polls == 1 && fk.errors == 1);
}

static void
check_limits(void)
{
	struct nvme_softc sc;
	struct sysmon_envsys sme = { .sme_cookie = &sc };
	envsys_data_t e = { 0 };
	sysmon_envsys_lim_t lim;
	uint32_t props;

	softc(&sc);
	sc.sc_identify.wctemp = 343;
	sc.sc_identify.cctemp = 358;
	memset(&lim, 0, sizeof(lim));
	props = PROP_OTHER;
	nvme_sensors_get_limits(&sme, &e, &lim, &props);
	CHECK(props == (PROP_OTHER | PROP_WARNMAX | PROP_CRITMAX));
	/* sysmon alarms strictly above; NVMe overheats at the limit. */
	CHECK(lim.sel_warnmax == 343000000 - 1);
	CHECK(lim.sel_critmax == 358000000 - 1);

	sc.sc_identify.wctemp = 0;
	sc.sc_identify.cctemp = 0;
	memset(&lim, 0, sizeof(lim));
	props = 0;
	nvme_sensors_get_limits(&sme, &e, &lim, &props);
	CHECK(props == 0 && lim.sel_warnmax == 0 && lim.sel_critmax == 0);

	sc.sc_identify.wctemp = 1001;
	sc.sc_identify.cctemp = 1000;
	props = 0;
	nvme_sensors_get_limits(&sme, &e, &lim, &props);
	CHECK(props == PROP_CRITMAX && lim.sel_critmax == 1000000000 - 1);
}

static void
check_attach_detach(void)
{
	struct nvme_softc sc;

	/* Registered: name, callbacks, temperature sensor with limits. */
	fake_reset();
	softc(&sc);
	sc.sc_smart = NULL;
	nvme_sensors_attach(&sc);
	CHECK(fk.allocs == 1 && fk.mem.size == NVM_LOG_PAGE_SMART_HEALTH_LEN);
	CHECK(sc.sc_smart == &fk.mem && sc.sc_sme == &fk.sme);
	CHECK(fk.sme.registered && fk.sme.sensor == &sc.sc_temp);
	CHECK(strcmp(fk.sme.sme_name, "nvme0") == 0);
	CHECK(fk.sme.sme_cookie == &sc);
	CHECK(fk.sme.sme_refresh == nvme_sensors_refresh);
	CHECK(fk.sme.sme_get_limits == nvme_sensors_get_limits);
	CHECK(sc.sc_temp.units == ENVSYS_STEMP);
	CHECK(sc.sc_temp.state == ENVSYS_SINVALID);
	CHECK(sc.sc_temp.flags == ENVSYS_FMONLIMITS);
	CHECK(strcmp(sc.sc_temp.desc, "composite temperature") == 0);
	CHECK(fk.errors == 0);

	/* Detach unregisters once and frees the page once. */
	nvme_sensors_detach(&sc);
	CHECK(fk.unregisters == 1 && fk.frees == 1 && fk.destroys == 0);
	CHECK(sc.sc_sme == NULL && sc.sc_smart == NULL);
	nvme_sensors_detach(&sc);
	CHECK(fk.unregisters == 1 && fk.frees == 1);

	/* No DMA memory: no sysmon object. */
	fake_reset();
	softc(&sc);
	sc.sc_smart = NULL;
	fk.no_mem = true;
	nvme_sensors_attach(&sc);
	CHECK(fk.creates == 0 && sc.sc_sme == NULL && sc.sc_smart == NULL);
	CHECK(fk.errors == 1);

	/* Sensor or registration failure: destroyed, page freed. */
	for (int i = 0; i < 2; i++) {
		fake_reset();
		softc(&sc);
		sc.sc_smart = NULL;
		if (i == 0)
			fk.attach_fails = true;
		else
			fk.register_fails = true;
		nvme_sensors_attach(&sc);
		CHECK(fk.creates == 1 && fk.destroys == 1);
		CHECK(fk.frees == 1);
		CHECK(sc.sc_sme == NULL && sc.sc_smart == NULL);
		CHECK(fk.errors == 1);
		nvme_sensors_detach(&sc);
		CHECK(fk.unregisters == 0 && fk.frees == 1);
	}
}

int
main(void)
{
	check_command();
	check_readings();
	check_limits();
	check_attach_detach();
	printf("nvme temperature contract: %d checks, %d failures\n", checks,
	    failures);
	return failures != 0;
}
