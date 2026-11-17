/* Origin: EmberBSD (AI-assisted). */
/*
 * Execute the actual SD host callback against a synchronous transport model.
 * The model replaces only USB I/O and the kernel mutex. Packet construction
 * and response decoding use the production urtsx_proto.c implementation.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "urtsx_proto.h"
#include "urtsxreg.h"
#include "sdmmc-flags.inc"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define atomic_load_relaxed(p) (*(p))
#define atomic_store_relaxed(p, v) (*(p) = (v))
typedef void *sdmmc_chipset_handle_t;
typedef void *device_t;
#define device_private(p) (p)
#define USB_TASKQ_DRIVER 1
struct urtsx_reg { uint16_t address; uint8_t mask, value; };
struct sdmmc_command {
	uint16_t c_opcode;
	uint32_t c_arg, c_resp[4];
	void *c_data;
	int c_datalen, c_blklen, c_flags, c_error, c_resid;
};
struct urtsx_softc {
	int sc_lock;
	bool sc_failed, sc_unready;
	bool sc_ready;
	bool sc_powered;
	bool sc_power_dirty;
	uint8_t sc_package;
	void *sc_udev;
	void *sc_in, *sc_out, *sc_ixfer, *sc_oxfer;
	int sc_poll;
	unsigned sc_status, sc_dying, sc_recovery_wait, sc_suspended;
	uint8_t sc_command[1024], sc_response[256];
};
static unsigned transfers, reads;
static int transfer_error, read_error_at;
static bool owned;
static unsigned recoveries;
static int recovery_error;
static unsigned controls, writes;
static int control_error_at;
static bool task_running, task_queued, child_stopped;
static unsigned legacy_registers;
static bool partial_power, full_power, output_enabled, fail_full_power;
static char phases[16];
static unsigned phase_count;
static uint32_t write_r1;
static int ppbuf_error;
static int flush_error;
static uint8_t write_status[4] = {0x60, 3, 0, 0};

static void mutex_enter(int *lock) { (void)lock; assert(!owned); owned = true; }
static void mutex_exit(int *lock) { (void)lock; assert(owned); owned = false; }
static int urtsx_recover(struct urtsx_softc *sc) {
	recoveries++;
	if (recovery_error) {
		sc->sc_unready = true;
		return recovery_error;
	}
	sc->sc_failed = false;
	sc->sc_unready = false;
	return 0;
}
static int
urtsx_submit(struct urtsx_softc *sc, struct urtsx_cmd *packet)
{
	assert(owned);
	transfers++;
	if (transfer_error)
		return transfer_error;
	assert(packet->count == 16 || packet->count == 20 || packet->count == 11);
	if (packet->count == 11) {
		phases[phase_count++] = 'D';
		memcpy(sc->sc_response, write_status, 4);
		return 0;
	}
	phases[phase_count++] = 'C';
	if (packet->buffer[11] == (0x40 | 24)) {
		sc->sc_response[0] = 0x60;
		sc->sc_response[1] = 24;
		for (unsigned i = 0; i < 4; i++)
			sc->sc_response[2 + i] = write_r1 >> (24 - 8 * i);
		sc->sc_response[6] = sc->sc_response[7] = 0;
		return 0;
	}
	const uint8_t response[] = {0x60, 8, 0, 0, 1, 0xaa, 0, 0};
	memcpy(sc->sc_response, response, sizeof(response));
	return 0;
}
static int
urtsx_read(struct urtsx_softc *sc, const uint16_t *registers, size_t count)
{
	assert(owned);
	reads++;
	if ((int)reads == read_error_at) {
		sc->sc_failed = true;
		return EIO;
	}
	if (count == 2 && registers[0] == URTSX_CARD_EXIST) {
		sc->sc_response[0] = sc->sc_response[1] = 0;
		return 0;
	}
	for (size_t i = 0; i < count; i++)
		sc->sc_response[i] = (uint8_t)(registers[i] - 0xfa00);
	return 0;
}
static int
urtsx_bulk(struct urtsx_softc *sc, bool input, void *data, size_t length)
{
	(void)sc;
	const uint8_t *bytes = data;
	const uint8_t prefix[] = {'R','T','C','R',2,2,0,0,0xfa,0,0,0};
	assert(owned && !input && length == 524);
	assert(memcmp(bytes, prefix, sizeof(prefix)) == 0);
	for (unsigned i = 0; i < 512; i++)
		assert(bytes[12 + i] == (uint8_t)i);
	phases[phase_count++] = 'P';
	return ppbuf_error;
}
static int
urtsx_control_write(struct urtsx_softc *sc, const struct urtsx_reg *reg)
{
	assert(owned);
	controls++;
	if (reg->address == URTSX_FIFO_CTL) {
		assert(reg->mask == 1 && reg->value == 1);
		phases[phase_count++] = 'F';
		if (flush_error)
			sc->sc_unready = true;
		return flush_error;
	}
	if ((int)controls == control_error_at) {
		sc->sc_unready = true;
		return EIO;
	}
	if (controls == 1)
		assert(reg->address == 0xfd54 && reg->mask == 4 && reg->value == 0);
	return 0;
}
static int
urtsx_write(struct urtsx_softc *sc, const struct urtsx_reg *regs, size_t count)
{
	writes++;
	for (size_t i = 0; i < count; i++) {
		const struct urtsx_reg *reg = &regs[i];
		if (reg->address == 0xfda0 && reg->mask == 0x0f && reg->value == 0)
			legacy_registers |= 1;
		if (reg->address == 0xfc2e && reg->mask == 0xff && reg->value == 0x24)
			legacy_registers |= 2;
		if (reg->address == 0xfda8 && reg->mask == 0xff && reg->value == 0)
			legacy_registers |= 4;
		if (reg->address == 0xfda7 && reg->mask == 0x08 && reg->value == 0)
			legacy_registers |= 8;
		if (reg->address == 0xfd7a && reg->mask == 3 && reg->value == 2)
			partial_power = true;
		if (reg->address == 0xfd7a && reg->mask == 0x0f && reg->value == 0) {
			assert(partial_power);
			if (fail_full_power) {
				sc->sc_failed = true;
				return EIO;
			}
			full_power = true;
		}
		if (reg->address == 0xfd54 && reg->mask == 4 && reg->value == 4) {
			assert(partial_power && full_power && legacy_registers == 15);
			output_enabled = true;
		}
	}
	return 0;
}
static void usbd_delay_ms(void *dev, unsigned ms) { (void)dev; (void)ms; }
static void usbd_abort_pipe(void *pipe) { (void)pipe; }
static int config_detach_children(device_t dev, int flags) {
	(void)dev; (void)flags;
	child_stopped = true;
	return 0;
}
static bool usb_rem_task(void *dev, int *task) {
	(void)dev; (void)task;
	bool removed = task_queued;
	task_queued = false;
	return removed;
}
static bool usb_rem_task_wait(void *dev, int *task, int queue, void *lock) {
	(void)dev; (void)task; (void)queue; (void)lock;
	assert(child_stopped);
	/* usb.c removes a queued task without waiting for its running copy. */
	if (task_queued) {
		task_queued = false;
		return true;
	}
	task_running = false; /* The wait joins the running handler. */
	return false;
}
static void usbd_destroy_xfer(void *xfer) {
	(void)xfer;
	assert(!task_running); /* The handler still owns its transfer until joined. */
}
static void usbd_close_pipe(void *pipe) { (void)pipe; assert(!task_running); }
static void mutex_destroy(int *lock) { (void)lock; assert(!task_running); }
static void pmf_device_deregister(device_t dev) { (void)dev; assert(child_stopped); }

#include "urtsx-power-helpers.inc"
#include "urtsx-power.inc"
#include "urtsx-detach.inc"

#include "urtsx-exec.inc"
#include "urtsx-poll.inc"

static void
reset_model(void)
{
	transfers = reads = 0;
	transfer_error = read_error_at = 0;
	recoveries = 0;
	recovery_error = 0;
	controls = writes = 0;
	control_error_at = 0;
	legacy_registers = 0;
	partial_power = full_power = output_enabled = fail_full_power = false;
	phase_count = 0;
	write_r1 = 0x100;
	ppbuf_error = 0;
	flush_error = 0;
	write_status[0] = 0x60; write_status[1] = 3;
	write_status[2] = write_status[3] = 0;
	assert(!owned);
}

int
main(void)
{
	/* R1 error and status interpretation belongs to the shared SD contract. */
	assert(SD_R1_WRITE_REJECTED(0x04000000U)); /* write protection */
	assert(SD_R1_WRITE_REJECTED(0x02000000U)); /* locked status blocks writes */
	assert(SD_R1_WRITE_REJECTED(0x00800100U)); /* CRC error even when READY */
	assert(!SD_R1_WRITE_REJECTED(0x00004100U)); /* READY + ECC-disabled status */
	assert(!SD_R1_WRITE_REJECTED(0x00060100U)); /* MMC-reserved SD bits */
	struct urtsx_softc sc = {0};
	uint8_t buffer[512];
	struct sdmmc_command command = {
		.c_opcode = 8, .c_arg = 0x1aa, .c_flags = SCF_RSP_R7,
	};
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == 0 && command.c_resp[0] == 0x1aa);
	assert((command.c_flags & SCF_ITSDONE) && command.c_resid == 0);

	reset_model();
	sc.sc_suspended = 1;
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == EBUSY && transfers == 0 && reads == 0);
	sc.sc_ready = true;
	urtsx_poll(&sc);
	assert(transfers == 0 && reads == 0 && recoveries == 0);
	sc.sc_suspended = 0;

	reset_model();
	command = (struct sdmmc_command){.c_opcode = 24, .c_data = buffer,
	    .c_datalen = 512, .c_flags = SCF_RSP_R1};
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == ENOTSUP && (command.c_flags & SCF_ITSDONE));
	assert(command.c_resid == 512 && transfers == 0 && reads == 0);
	assert(!sc.sc_failed);

	reset_model();
	sc = (struct urtsx_softc){.sc_status = 1};
	for (unsigned i = 0; i < sizeof(buffer); i++)
		buffer[i] = (uint8_t)i;
	command = (struct sdmmc_command){.c_opcode = 24, .c_data = buffer,
	    .c_datalen = 512, .c_blklen = 512, .c_flags = SCF_RSP_R1 | SCF_CMD_ADTC};
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == 0 && command.c_resid == 0);
	assert(command.c_resp[0] == 0x100 && (command.c_flags & SCF_ITSDONE));
	assert(phase_count == 4 && memcmp(phases, "CPDF", 4) == 0);

	const unsigned blocked_status[] = {0, 9, 0x11};
	const int blocked_error[] = {ENXIO, EROFS, EIO};
	for (unsigned i = 0; i < 3; i++) {
		reset_model();
		sc = (struct urtsx_softc){.sc_status = blocked_status[i]};
		urtsx_exec_command(&sc, &command);
		assert(command.c_error == blocked_error[i]);
		assert(command.c_resid == 512 && phase_count == 0);
	}
	reset_model();
	sc = (struct urtsx_softc){.sc_status = 1};
	write_r1 = 0x04000100; /* WP rejected, even though READY is set. */
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == EIO && command.c_resid == 512);
	assert(phase_count == 1 && phases[0] == 'C');
	reset_model();
	sc = (struct urtsx_softc){.sc_status = 1};
	ppbuf_error = ETIMEDOUT;
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == ETIMEDOUT && command.c_resid == 512 && sc.sc_failed);
	assert(phase_count == 2 && memcmp(phases, "CP", 2) == 0);
	reset_model();
	sc = (struct urtsx_softc){.sc_status = 1};
	write_status[2] = 0x20;
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == EIO && command.c_resid == 512);
	assert(phase_count == 3 && memcmp(phases, "CPD", 3) == 0);
	reset_model();
	sc = (struct urtsx_softc){.sc_status = 1};
	flush_error = EIO;
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == EIO && command.c_resid == 512 && sc.sc_unready);
	assert(phase_count == 4 && memcmp(phases, "CPDF", 4) == 0);
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == EIO && phase_count == 4); /* no implicit replay */
	sc = (struct urtsx_softc){0};

	/* Data-less erase, lock and persistent switches must not reach USB. */
	const uint16_t destructive[] = {6, 28, 29, 32, 33, 35, 36, 38, 42, 56};
	for (unsigned i = 0; i < sizeof(destructive) / sizeof(destructive[0]); i++) {
		reset_model();
		command = (struct sdmmc_command){.c_opcode = destructive[i],
		    .c_flags = SCF_RSP_R1};
		urtsx_exec_command(&sc, &command);
		assert(command.c_error == ENOTSUP && (command.c_flags & SCF_ITSDONE));
		assert(transfers == 0 && reads == 0);
	}

	reset_model();
	command = (struct sdmmc_command){.c_opcode = 17, .c_datalen = 512,
	    .c_flags = SCF_RSP_R1 | SCF_CMD_READ};
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == EINVAL && transfers == 0 && reads == 0);

	reset_model();
	memset(buffer, 0xff, sizeof(buffer));
	command = (struct sdmmc_command){.c_opcode = 17, .c_data = buffer,
	    .c_datalen = 512, .c_flags = SCF_RSP_R1 | SCF_CMD_READ};
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == 0 && command.c_resid == 0 && reads == 4);
	for (unsigned i = 0; i < sizeof(buffer); i++)
		assert(buffer[i] == (uint8_t)i);

	reset_model();
	read_error_at = 2;
	memset(buffer, 0xff, sizeof(buffer));
	command.c_flags &= ~SCF_ITSDONE;
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == EIO && command.c_resid == 384 && sc.sc_failed);
	assert(command.c_flags & SCF_ITSDONE);
	assert(buffer[127] == 127 && buffer[128] == 0xff);

	reset_model();
	sc.sc_failed = false;
	transfer_error = ETIMEDOUT;
	command.c_flags &= ~SCF_ITSDONE;
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == ETIMEDOUT && command.c_resid == 512);
	assert(sc.sc_failed && (command.c_flags & SCF_ITSDONE));
	assert(reads == 0);

	reset_model();
	sc.sc_unready = true;
	command.c_flags &= ~SCF_ITSDONE;
	urtsx_exec_command(&sc, &command);
	assert(command.c_error == EIO && transfers == 0 && reads == 0);

	reset_model();
	sc = (struct urtsx_softc){.sc_ready = true, .sc_status = 1};
	read_error_at = 1;
	urtsx_poll(&sc);
	assert(sc.sc_status == 1 && sc.sc_failed); /* no false removal */
	read_error_at = 0;
	urtsx_poll(&sc);
	assert(recoveries == 1 && reads == 2 && sc.sc_status == 0);

	reset_model();
	sc = (struct urtsx_softc){.sc_ready = true, .sc_status = 1, .sc_failed = true};
	recovery_error = EIO;
	urtsx_poll(&sc);
	assert(sc.sc_status == 1 && sc.sc_unready && recoveries == 1);
	for (unsigned i = 0; i < 5; i++)
		urtsx_poll(&sc);
	assert(recoveries == 1 && reads == 0); /* bounded backoff */
	recovery_error = 0;
	urtsx_poll(&sc);
	assert(recoveries == 2 && !sc.sc_unready && sc.sc_status == 0);
	sc.sc_dying = 1;
	urtsx_poll(&sc);
	assert(recoveries == 2 && reads == 1);

	reset_model();
	sc = (struct urtsx_softc){.sc_unready = true, .sc_failed = true,
	    .sc_powered = true};
	assert(urtsx_bus_power(&sc, 0) == 0);
	assert(!sc.sc_powered && controls >= 4); /* OFF bypasses failed bulk queue */
	reset_model();
	sc.sc_powered = true;
	control_error_at = 2;
	assert(urtsx_bus_power(&sc, 0) == EIO);
	assert(sc.sc_powered); /* An incomplete OFF must not claim success. */

	reset_model();
	sc = (struct urtsx_softc){.sc_status = 1};
	assert(urtsx_bus_power(&sc, MMC_OCR_3_2V_3_3V) == 0);
	assert(sc.sc_powered && output_enabled && legacy_registers == 15);
	reset_model();
	sc = (struct urtsx_softc){.sc_status = 1};
	fail_full_power = true;
	assert(urtsx_bus_power(&sc, MMC_OCR_3_2V_3_3V) == EIO);
	assert(!sc.sc_powered && !output_enabled && controls >= 4);

	/* A failed final pull write leaves power unknown, even after transport reset. */
	reset_model();
	sc = (struct urtsx_softc){.sc_status = 1, .sc_powered = true};
	control_error_at = 10;
	assert(urtsx_bus_power(&sc, 0) == EIO);
	int recovered = urtsx_recover(&sc);
	assert(recovered == 0);
	reset_model();
	assert(urtsx_bus_power(&sc, MMC_OCR_3_2V_3_3V) == 0);
	assert(controls >= 4 && full_power && output_enabled);
	assert(sc.sc_powered && !sc.sc_power_dirty);

	reset_model();
	sc.sc_in = sc.sc_out = sc.sc_ixfer = sc.sc_oxfer = &sc;
	task_running = task_queued = true;
	child_stopped = false;
	assert(urtsx_detach(&sc, 0) == 0);
	assert(!task_running && !task_queued && child_stopped);
	puts("urtsx host callback: PASS");
	return 0;
}
