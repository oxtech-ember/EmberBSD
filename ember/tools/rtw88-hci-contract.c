/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD HCI ownership and uncertain-publication contracts. */
#include <dev/ic/rtw88_hci.h>
#include <dev/ic/rtw88_async.h>

/* Reuse the actual startup, MMIO, DMA and firmware fixture. */
#define main rtw88_start_contract_main
#include "rtw88-start-contract.c"
#undef main

static void
test_hci_required_operations(void)
{
	struct rtw88_softc sc;
	struct rtw88_hci_ops incomplete;

	reset_bus(&sc);
	sc.sc_hci = NULL;
	CHECK(rtw88_core_attach(&sc) == EINVAL);
	CHECK(f.alloc_calls == 0);
	CHECK(sc.sc_nring == 0 && !sc.sc_resources_ready);

	reset_bus(&sc);
	incomplete = rtw88_pci_hci_ops;
	incomplete.irq_can_run_inline = false;
	sc.sc_hci = &incomplete;
	CHECK(rtw88_core_attach(&sc) == EINVAL);
	CHECK(f.alloc_calls == 0);

	reset_bus(&sc);
	incomplete = rtw88_pci_hci_ops;
	incomplete.xmit = NULL;
	sc.sc_hci = &incomplete;
	CHECK(rtw88_core_attach(&sc) == EINVAL);
	CHECK(f.alloc_calls == 0);
	CHECK(sc.sc_nring == 0 && !sc.sc_resources_ready);
}

static void
test_pci_descriptor_geometry(void)
{
	struct rtw88_softc sc;
	struct rtw88_chip_ops chip;

	/* Unsupported nonzero sizes pass generic chip validation, but the PCI
	 * fixed-ring transport must refuse them before allocating any DMA. */
	for (unsigned which = 0; which < 2; which++) {
		reset_bus(&sc);
		chip = rtw8822c_chip_ops;
		if (which == 0)
			chip.caps.tx_desc_size++;
		else
			chip.caps.rx_desc_size++;
		sc.sc_chip = &chip;
		CHECK(rtw88_chip_valid(&sc));
		CHECK(rtw88_core_attach(&sc) == EOPNOTSUPP);
		CHECK(f.alloc_calls == 0);
		CHECK(sc.sc_nring == 0 && !sc.sc_resources_ready);
	}
}

static void
test_hci_sdio_extensions_default_off(void)
{
	struct rtw88_softc sc;
	struct rtw88_hci_ops hci;

	reset_bus(&sc);
	hci = rtw88_pci_hci_ops;
	CHECK(!hci.no_host_dma && hci.write_window == NULL &&
	    hci.set_data_mode == NULL);
	sc.sc_hci = &hci;
	CHECK(rtw88_hci_valid(&sc));
	hci.xmit = NULL;
	CHECK(!rtw88_hci_valid(&sc));
}

static int injected_error;
static enum rtw88_tx_progress injected_progress;
static int
injected_xmit(struct rtw88_softc *s,unsigned q,const uint8_t *p,size_t n,
    const struct rtw88_tx_info *i,enum rtw88_tx_progress *progress)
{ (void)s;(void)q;(void)p;(void)n;(void)i;*progress=injected_progress;return injected_error; }
static void
test_executor_tx_progress(void)
{
 struct rtw88_softc sc; struct rtw88_hci_ops hci;
 struct rtw88_tx_job job={0}; enum rtw88_tx_progress progress;
 if (!start_ok(&sc)) return;
 hci=rtw88_pci_hci_ops; hci.xmit=injected_xmit;sc.sc_hci=&hci;
 job.id.generation=sc.sc_generation;job.q=RTW88_TXQ_BE;
 job.mode=RTW88_TX_MODE_RAW;job.length=24;job.bytes[0]=0x08;job.bytes[1]=1;
 injected_error=ENOBUFS;injected_progress=RTW88_TX_NOT_STARTED;
 CHECK(rtw88_tx_execute(&sc,&job,&progress)==EBUSY && progress==RTW88_TX_NOT_STARTED);
 injected_error=EIO;injected_progress=RTW88_TX_MAY_HAVE_WRITTEN;
 CHECK(rtw88_tx_execute(&sc,&job,&progress)==EIO && progress==RTW88_TX_MAY_HAVE_WRITTEN);
 injected_error=0;injected_progress=RTW88_TX_NOT_STARTED;
 CHECK(rtw88_tx_execute(&sc,&job,&progress)==EPROTO && progress==RTW88_TX_MAY_HAVE_WRITTEN);
 sc.sc_hci=&rtw88_pci_hci_ops;CHECK(rtw88_stop(&sc)==0);CHECK(rtw88_core_detach(&sc)==0);
}

static unsigned deferred_irqs, forbidden_irq_io;

static int
failed_hci_stop(struct rtw88_softc *sc)
{
	(void)sc;
	/* An HCI error must not depend on the PCI-specific DMA error field. */
	return EIO;
}

static int
failed_hci_stop_after_dma_disable(struct rtw88_softc *sc)
{
	int error = rtw88_pci_hci_ops.stop(sc);

	return error != 0 ? error : EIO;
}

static void
test_hci_stop_failure_retains_resources(void)
{
	struct rtw88_softc sc;
	struct rtw88_hci_ops hci;

	for (unsigned phase = 0; phase < 3; phase++) {
		if (!start_ok(&sc))
			return;
		hci = rtw88_pci_hci_ops;
		if (phase == 0)
			hci.quiesce = failed_hci_stop;
		else if (phase == 1)
			hci.stop = failed_hci_stop;
		else
			hci.stop = failed_hci_stop_after_dma_disable;
		sc.sc_hci = &hci;
		CHECK(rtw88_core_detach(&sc) == EIO);
		CHECK(sc.sc_resources_ready && sc.sc_nring == RTW88_NRING);
		if (phase < 2)
			CHECK(sc.sc_fw_dma_owned && sc.sc_dma_master);
		else
			CHECK(!sc.sc_fw_dma_owned && !sc.sc_dma_master &&
			    !sc.sc_power_dirty && sc.sc_dma_error == 0);
		CHECK(rtw88_start(&sc) == EBUSY);
		sc.sc_hci = &rtw88_pci_hci_ops;
		CHECK(rtw88_core_detach(&sc) == 0);
	}
}

static void
test_hci_optional_packet_command(void)
{
	struct rtw88_softc sc;
	struct rtw88_hci_ops hci;
	struct rtw88_bus_ops fault;
	uint8_t packet[32] = { 1, 0xff, 0x11, 0, 16 };

	reset_bus(&sc);
	hci = rtw88_pci_hci_ops;
	hci.fw_packet = NULL;
	fault = ops;
	/* Absence must be reported before the backend can access hardware. */
	fault.read_2 = NULL;
	fault.read_4 = NULL;
	fault.write_2 = NULL;
	sc.sc_hci = &hci;
	sc.sc_ops = &fault;
	CHECK(rtw88_fw_packet(&sc, packet, sizeof(packet)) == EOPNOTSUPP);
	CHECK(f.alloc_calls == 0);
}

static void
defer_irq(struct rtw88_softc *sc)
{
	CHECK(sc->sc_running);
	deferred_irqs++;
}

static int
forbidden_read_4(void *cookie, uint32_t reg, uint32_t *out)
{
	(void)cookie;
	(void)reg;
	(void)out;
	forbidden_irq_io++;
	return EIO;
}

static void
test_hci_sleeping_interrupt_is_deferred(void)
{
	struct rtw88_softc sc;
	struct rtw88_hci_ops sleeping;
	struct rtw88_bus_ops fault;
	unsigned reads, writes, syncs, delays;

	if (!start_ok(&sc))
		return;
	sleeping = rtw88_pci_hci_ops;
	sleeping.irq_can_run_inline = false;
	fault = ops;
	fault.read_4 = forbidden_read_4;
	sc.sc_hci = &sleeping;
	sc.sc_defer_irq = defer_irq;
	sc.sc_ops = &fault;
	deferred_irqs = forbidden_irq_io = 0;
	reads = f.reads;
	writes = f.writes;
	syncs = f.syncs;
	delays = f.delays;
	CHECK(rtw88_intr(&sc) == 1);
	CHECK(deferred_irqs == 1 && forbidden_irq_io == 0);
	CHECK(f.reads == reads && f.writes == writes);
	CHECK(f.syncs == syncs && f.delays == delays);
	sc.sc_ops = &ops;
	sc.sc_hci = &rtw88_pci_hci_ops;
	CHECK(rtw88_core_detach(&sc) == 0);
}

static void
test_hci_doorbell_failure_keeps_publication_uncertain(void)
{
	struct rtw88_softc sc;
	struct rtw88_bus_ops fault;
	struct rtw88_tx_info info;
	enum rtw88_tx_progress progress;
	uint8_t frame[64] = { 0x08, 0x01 };

	for (int after = 0; after <= 1; after++) {
		if (!start_ok(&sc))
			return;
		memset(&info, 0, sizeof(info));
		info.packet_size = sizeof(frame);
		info.flags = RTW88_TX_USE_RATE | RTW88_TX_DIS_RATE_FB;
		fault = ops;
		posted_fail_idx = 0x3a8; /* BE producer, independent wire fixture. */
		posted_fail_after = after;
		fault.write_2 = posted_write_2;
		sc.sc_ops = &fault;
		progress = RTW88_TX_NOT_STARTED;
		CHECK(sc.sc_hci->xmit(&sc, RTW88_TXQ_BE, frame, sizeof(frame),
		    &info, &progress) == EIO);
		CHECK(progress == RTW88_TX_MAY_HAVE_WRITTEN);
		CHECK(sc.sc_txq[RTW88_TXQ_BE].queued == 1);
		sc.sc_ops = &ops;
		CHECK(rtw88_core_detach(&sc) == 0);
	}
}

int
main(void)
{
	if (rtw88_start_contract_main() != EXIT_SUCCESS)
		return EXIT_FAILURE;
	test_hci_required_operations();
	test_pci_descriptor_geometry();
	test_hci_sdio_extensions_default_off();
	test_hci_stop_failure_retains_resources();
	test_hci_optional_packet_command();
	test_executor_tx_progress();
	test_hci_sleeping_interrupt_is_deferred();
	test_hci_doorbell_failure_keeps_publication_uncertain();
	printf("rtw88 HCI contract: %u assertions, %u failures\n",
	    assertions, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
