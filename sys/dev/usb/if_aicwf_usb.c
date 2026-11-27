/* SPDX-License-Identifier: BSD-2-Clause */
/* AIC8800DC FullMAC station on a configured USB WLAN interface. */
#include <sys/param.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/queue.h>
#include <sys/systm.h>

#include <sys/endian.h>
#include <dev/ic/aicwf_command.h>
#include <dev/ic/aicwfvar.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/uaicfwvar.h>

struct aicwf_usb_packet {
	SIMPLEQ_ENTRY(aicwf_usb_packet) link;
	size_t len;
	uint64_t serial;
	uint8_t data[];
};

struct aicwf_usb_softc {
	struct aicwf_softc wifi;
	struct uaicfw_attach_args bus;
	kmutex_t lock;
	struct workqueue *rxq;
	struct work rxwork;
	SIMPLEQ_HEAD(, aicwf_usb_packet) packets;
	unsigned count;
	bool scheduled;
	bool prepared;
};

static int aicwf_usb_match(device_t, cfdata_t, void *);
static void aicwf_usb_attach(device_t, device_t, void *);
static int aicwf_usb_detach(device_t, int);

CFATTACH_DECL_NEW(aicwf_usb, sizeof(struct aicwf_usb_softc),
    aicwf_usb_match, aicwf_usb_attach, aicwf_usb_detach, NULL);

/* Key admission and the port CFM update this lifetime at IPL_NET. */
static uint64_t
aicwf_usb_port(void *cookie)
{
	struct aicwf_softc * const sc = cookie;
	uint64_t serial = 0;

	mutex_spin_enter(&sc->sc_epoch_lock);
	if (sc->sc_ptk_valid && sc->sc_rx_active &&
	    sc->sc_port_serial == sc->sc_rx_serial)
		serial = sc->sc_port_serial;
	mutex_spin_exit(&sc->sc_epoch_lock);
	return serial;
}

static int
aicwf_usb_write(struct aicwf_softc *sc, size_t len, u_int reserve,
    bool *attempted)
{
	struct aicwf_usb_softc * const usc = sc->sc_bus;

	if (len >= AICWF_HDR_LEN && sc->sc_txbuf[2] == AICWF_TYPE_DATA) {
		struct uaicfw_port port = { .current = aicwf_usb_port, .cookie = sc };

		if (attempted != NULL)
			*attempted = false;
		if (sc->sc_receive_only || !sc->sc_connected)
			return ENETDOWN;
		if (len >= AICWF_HDR_LEN + AICWF_TXDESC_LEN &&
		    (sc->sc_ic.ic_flags & IEEE80211_F_WPA) != 0 &&
		    be16dec(sc->sc_txbuf + AICWF_HDR_LEN + 20) != ETHERTYPE_PAE) {
			port.serial = aicwf_usb_port(sc);
			if (port.serial == 0)
				return EACCES;
		}
		return usc->bus.ethernet(usc->bus.cookie, sc->sc_txbuf, len,
		    port.serial != 0 ? &port : NULL, attempted);
	}
	return usc->bus.write(usc->bus.cookie, sc->sc_txbuf, len, attempted);
}

/* Reserve queue space for the command or scan completion in either pipe. */
static unsigned
aicwf_usb_queue_limit(const uint8_t *p, size_t len, bool receive_only)
{
	uint16_t id, plen;

	if (len < AICWF_HDR_LEN)
		return 0;
	const uint8_t type = p[2] & 0x7f;
	if ((type & AICWF_TYPE_CFG) == 0)
		return receive_only ? 0 : 16;
	if (type == AICWF_TYPE_TX_CFM)
		return !receive_only && len == AICWF_HDR_LEN + 8 ? 32 : 0;
	if (type != AICWF_TYPE_MSG ||
	    !aicwf_reply_decode(p + AICWF_HDR_LEN, len - AICWF_HDR_LEN,
	    &id, &plen))
		return 0;
	switch (id) {
	case AICWF_MM_GET_MAC_ADDR_CFM:
	case AICWF_MM_ADD_IF_CFM:
	case AICWF_MM_REMOVE_IF_CFM:
	case AICWF_SCANU_START_ACK:
	case AICWF_SCANU_START_CFM:
		return 32;
	case AICWF_SCANU_RESULT_IND:
		return 24;
	default:
		return receive_only ? 0 : 32;
	}
}

static uint64_t
aicwf_usb_serial(void *cookie)
{
	struct aicwf_usb_softc * const usc = cookie;
	struct aicwf_softc * const sc = &usc->wifi;
	uint64_t serial;

	mutex_spin_enter(&sc->sc_epoch_lock);
	serial = sc->sc_rx_serial;
	mutex_spin_exit(&sc->sc_epoch_lock);
	return serial;
}

/* The USB callback cannot take net80211's sleep locks. */
static void
aicwf_usb_input(void *cookie, const uint8_t *p, size_t len, uint64_t serial)
{
	struct aicwf_usb_softc * const usc = cookie;
	struct aicwf_softc * const sc = &usc->wifi;
	struct aicwf_usb_packet *packet;

	mutex_enter(&usc->lock);
	if (usc->count >= aicwf_usb_queue_limit(p, len, sc->sc_receive_only)) {
		mutex_exit(&usc->lock);
		return;
	}
	packet = kmem_intr_alloc(sizeof(*packet) + len, KM_NOSLEEP);
	if (packet != NULL) {
		packet->len = len;
		memcpy(packet->data, p, len);
		packet->serial = serial;
		SIMPLEQ_INSERT_TAIL(&usc->packets, packet, link);
		usc->count++;
		if (!usc->scheduled) {
			usc->scheduled = true;
			workqueue_enqueue(usc->rxq, &usc->rxwork, NULL);
		}
	}
	mutex_exit(&usc->lock);
}

static void
aicwf_usb_rx_work(struct work *wk, void *arg)
{
	struct aicwf_usb_softc * const usc = arg;
	struct aicwf_usb_packet *packet;

	mutex_enter(&usc->lock);
	while ((packet = SIMPLEQ_FIRST(&usc->packets)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&usc->packets, link);
		usc->count--;
		mutex_exit(&usc->lock);
		if (!atomic_load_relaxed(&usc->wifi.sc_dying))
			aicwf_rx(&usc->wifi, packet->data, packet->len, packet->serial);
		kmem_intr_free(packet, sizeof(*packet) + packet->len);
		mutex_enter(&usc->lock);
	}
	usc->scheduled = false;
	mutex_exit(&usc->lock);
}

static int
aicwf_usb_match(device_t parent, cfdata_t cf, void *aux)
{
	return 1;
}

static void
aicwf_usb_attach(device_t parent, device_t self, void *aux)
{
	struct aicwf_usb_softc * const usc = device_private(self);
	static const struct aicwf_bus_ops ops = { .write = aicwf_usb_write };
	struct aicwf_softc * const sc = &usc->wifi;
	int error;

	aprint_naive("\n");
	aprint_normal(": AIC8800DC station, passive receive only\n");
	usc->bus = *(struct uaicfw_attach_args *)aux;
	mutex_init(&usc->lock, MUTEX_DEFAULT, IPL_SOFTUSB);
	SIMPLEQ_INIT(&usc->packets);
	aicwf_prepare(sc, self, &ops, usc, true);
	sc->sc_receive_only = true;
	usc->prepared = true;
	error = workqueue_create(&usc->rxq, "aicwfrx", aicwf_usb_rx_work, usc,
	    PRI_NONE, IPL_SOFTUSB, WQ_MPSAFE);
	if (error != 0)
		goto fail;
	usc->bus.listen(usc->bus.cookie, aicwf_usb_input, aicwf_usb_serial,
	    usc);
	error = aicwf_station_setup(sc);
	if (error != 0)
		goto fail;
	error = aicwf_ifattach(sc);
	if (error == 0)
		return;
fail:
	aprint_error_dev(self, "station setup failed: %d\n", error);
	(void)aicwf_usb_detach(self, 0);
}

static int
aicwf_usb_detach(device_t self, int flags)
{
	struct aicwf_usb_softc * const usc = device_private(self);

	if (!usc->prepared)
		return 0;
	usc->bus.listen(usc->bus.cookie, NULL, NULL, NULL);
	aicwf_quiesce(&usc->wifi);
	if (usc->rxq != NULL) {
		workqueue_wait(usc->rxq, &usc->rxwork);
		workqueue_destroy(usc->rxq);
	}
	aicwf_release(&usc->wifi);
	mutex_destroy(&usc->lock);
	usc->prepared = false;
	return 0;
}
