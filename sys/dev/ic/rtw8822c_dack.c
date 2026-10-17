/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88 DACK. */
/*
 * Cold ADCK/DACK only. Native safety deviations: validate complete and
 * range-checked measurements, use signed widened arithmetic, reject
 * unrepresentable vectors, propagate failures and keep RF backups separate
 * for paths AB.
 * Residual nonconvergence after ten valid attempts is advisory, as in the
 * reference. Report measured, converged and completed paths separately.
 * No warm cache. A failure after a write requires a full caller power reset;
 * restoring control words cannot undo internal ADC/DAC/MSBK calibration.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>

#define SAMPLE_COUNT 100U
#define SAMPLE_READ_BUDGET 10202U
#define RF_MASK 0xfffffU
/* 80 measurements * 10202 sample reads + 80000 ready polls + <10000
 * control/RF operations fit in 1000000. Explicit step delays total at most
 * 1040000 us, successful polls <800000 us, RF delays <10000 us. */
#define OP_BUDGET 1000000U
#define DELAY_BUDGET 2000000U

struct dack_context {
	struct rtw88_softc *sc;
	struct rtw8822c_dack_result *out;
	uint32_t generation, adck[2];
	int error, mutated;
};

static void
stage(struct dack_context *c, unsigned value, unsigned path)
{

	if (c->error == 0) {
		c->out->stage = (uint8_t)value;
		c->out->path = (uint8_t)path;
	}
}

static int
canceled(const struct dack_context *c)
{

	return c->sc->sc_stop_requested ||
	    c->sc->sc_generation != c->generation;
}

static int
begin(struct dack_context *c)
{

	if (c->error != 0)
		return 0;
	if (canceled(c)) {
		c->error = EINTR;
		return 0;
	}
	if (c->out->operations == OP_BUDGET) {
		c->error = E2BIG;
		return 0;
	}
	c->out->operations++;
	return 1;
}

static void
complete(struct dack_context *c, int error)
{

	if (error != 0)
		c->error = error;
	else if (canceled(c))
		c->error = EINTR;
}

static uint32_t
rd(struct dack_context *c, uint32_t addr)
{
	uint32_t value = 0;

	if (begin(c))
		complete(c, c->sc->sc_ops->read_4(c->sc->sc_bus, addr, &value));
	return value;
}

static void
wr(struct dack_context *c, uint32_t addr, unsigned width, uint32_t value)
{
	int error;

	if (!begin(c))
		return;
	/* A failing bus write can already have changed the hardware. */
	c->mutated = 1;
	if (width == 1)
		error = c->sc->sc_ops->write_1(c->sc->sc_bus, addr, (uint8_t)value);
	else
		error = c->sc->sc_ops->write_4(c->sc->sc_bus, addr, value);
	complete(c, error);
}

static void
wait_us(struct dack_context *c, unsigned us)
{

	if (c->error != 0)
		return;
	if (canceled(c)) {
		c->error = EINTR;
		return;
	}
	if (us > DELAY_BUDGET - c->out->delay_us) {
		c->error = ETIMEDOUT;
		return;
	}
	c->out->delay_us += us;
	c->sc->sc_ops->delay_us(c->sc->sc_bus, us);
	if (canceled(c))
		c->error = EINTR;
}

static void
mask32(struct dack_context *c, uint32_t addr, uint32_t mask, uint32_t value)
{
	uint32_t old = rd(c, addr);

	wr(c, addr, 4, (old & ~mask) |
	    ((value << __builtin_ctz(mask)) & mask));
}

static uint32_t
read_mask(struct dack_context *c, uint32_t addr, uint32_t mask)
{

	return (rd(c, addr) & mask) >> __builtin_ctz(mask);
}

static uint32_t
read_rf(struct dack_context *c, unsigned path, uint32_t addr)
{

	return read_mask(c, (path ? 0x4c00 : 0x3c00) + addr * 4, RF_MASK);
}

static void
write_rf(struct dack_context *c, unsigned path, uint32_t addr,
    uint32_t mask, uint32_t value)
{

	if (addr == 0) {
		wr(c, path ? 0x4108 : 0x1808, 4, value & RF_MASK);
		wait_us(c, 13);
	} else {
		mask32(c, (path ? 0x4c00 : 0x3c00) + addr * 4, mask, value);
		wait_us(c, 1);
	}
}

static void
ready(struct dack_context *c, uint32_t addr, uint32_t mask, uint32_t target)
{
	unsigned i;

	for (i = 0; i < 1000 && c->error == 0; i++) {
		if (read_mask(c, addr, mask) == target && c->error == 0)
			return;
		wait_us(c, 10);
	}
	if (c->error == 0)
		c->error = ETIMEDOUT;
}

static int
signed10(uint32_t value)
{

	value &= 1023;
	return value < 512 ? (int)value : (int)value - 1024;
}

static void
sort(int values[SAMPLE_COUNT])
{
	unsigned i, j;

	for (i = 1; i < SAMPLE_COUNT; i++) {
		int value = values[i];

		for (j = i; j > 0 && values[j - 1] > value; j--)
			values[j] = values[j - 1];
		values[j] = value;
	}
}

static int
pair(struct dack_context *c, int *iv, int *qv)
{
	uint32_t raw;

	if (c->error == 0)
		c->out->sample_reads++;
	raw = read_mask(c, 0x2dbc, 0x3fffff);

	*iv = signed10(raw >> 12);
	*qv = signed10(raw);
	return *iv >= -100 && *iv <= 100 && *qv >= -100 && *qv <= 100;
}

static void
valid_pair(struct dack_context *c, int *iv, int *qv)
{

	while (c->error == 0 && c->out->sample_reads < SAMPLE_READ_BUDGET) {
		if (pair(c, iv, qv))
			return;
	}
	if (c->error == 0)
		c->error = ERANGE;
}

static void
measure(struct dack_context *c, int *iv, int *qv)
{
	int samples[2][SAMPLE_COUNT], si, sq;
	int32_t isum = 0, qsum = 0;
	unsigned count = 0, reads, attempt, i;

	if (c->error != 0)
		return;
	c->out->sample_stage = c->out->stage;
	c->out->sample_path = c->out->path;
	c->out->sample_count = c->out->sample_reads = 0;
	c->out->filter_passes = 0;
	c->out->spread[0] = c->out->spread[1] = 0;
	c->out->mean[0] = c->out->mean[1] = 0;
	c->out->mean_valid = 0;
	/* Preserve the reference RF-mode diagnostic reads in the source trace. */
	(void)read_rf(c, 0, 0);
	(void)read_rf(c, 1, 0);
	for (reads = 0; count < SAMPLE_COUNT && reads < 10000 &&
	    c->error == 0; reads++) {
		if (pair(c, &si, &sq)) {
			samples[0][count] = si;
			samples[1][count++] = sq;
			c->out->sample_count = (uint16_t)count;
		}
	}
	if (c->error != 0)
		return;
	if (count != SAMPLE_COUNT) {
		c->error = ERANGE;
		return;
	}
	for (attempt = 0; attempt < 101 && c->error == 0; attempt++) {
		sort(samples[0]);
		sort(samples[1]);
		c->out->spread[0] = (uint16_t)(samples[0][99] - samples[0][0]);
		c->out->spread[1] = (uint16_t)(samples[1][99] - samples[1][0]);
		if (samples[0][99] - samples[0][0] <= 5 &&
		    samples[1][99] - samples[1][0] <= 5)
			break;
		c->out->filter_passes++;
		/*
		 * Skip outliers within the existing total request bound. Neither
		 * invalid replacement can enter the trimmed mean.
		 */
		valid_pair(c, &samples[0][0], &samples[1][0]);
		valid_pair(c, &samples[0][99], &samples[1][99]);
		if (c->error != 0)
			return;
	}
	if (c->error != 0)
		return;
	/*
	 * The reference bounds its noise filter, then takes this trimmed mean.
	 * A spread above five is not a calibration failure. ADC/DAC residuals
	 * must still converge below five in their separate bounded loops.
	 * The last replacement changes only the two excluded extremes.
	 */
	for (i = 10; i < 90; i++) {
		isum += samples[0][i];
		qsum += samples[1][i];
	}
	*iv = isum / 80;
	*qv = qsum / 80;
	c->out->mean[0] = (int16_t)*iv;
	c->out->mean[1] = (int16_t)*qv;
	c->out->mean_valid = 1;
}

static unsigned
write_base(unsigned path)
{

	return path ? 0x4100 : 0x1800;
}

static unsigned
read_base(unsigned path)
{

	return path ? 0x4500 : 0x2800;
}

#define w32(c, a, v) wr(c, a, 4, v)
#define w8(c, a, v) wr(c, a, 1, v)

#define DACK_BIT(n) (1U << (n))
static void
bb_setting(struct dack_context *c)
{
	mask32(c, 0x1d58, 0xff8, 0x1ff);
	mask32(c, 0x1a00, 0x3, 0x2);
	mask32(c, 0x1a14, 0x300, 0x3);
	w32(c, 0x1d70, 0x7e7e7e7e);
	mask32(c, 0x180c, 0x3, 0x0);
	mask32(c, 0x410c, 0x3, 0x0);
	w32(c, 0x1b00, 0x00000008);
	w8(c, 0x1bcc, 0x3f);
	w32(c, 0x1b00, 0x0000000a);
	w8(c, 0x1bcc, 0x3f);
	mask32(c, 0x1e24, DACK_BIT(31), 0x0);
	mask32(c, 0x1e28, 0xf, 0x3);
}
static void
step1(struct dack_context *c, unsigned path)
{
	uint32_t base_addr;
	uint32_t read_addr;

	base_addr = write_base(path);
	read_addr = read_base(path);

	w32(c, base_addr + 0x68, c->adck[path]);
	w32(c, base_addr + 0x0c, 0xdff00220);
	if (path == 0) {
		w32(c, base_addr + 0x60, 0xf0040ff0);
		w32(c, 0x1c38, 0xffffffff);
	}
	w32(c, base_addr + 0x10, 0x02d508c5);
	w32(c, 0x9b4, 0xdb66db00);
	w32(c, base_addr + 0xb0, 0x0a11fb88);
	w32(c, base_addr + 0xbc, 0x0008ff81);
	w32(c, base_addr + 0xc0, 0x0003d208);
	w32(c, base_addr + 0xcc, 0x0a11fb88);
	w32(c, base_addr + 0xd8, 0x0008ff81);
	w32(c, base_addr + 0xdc, 0x0003d208);
	w32(c, base_addr + 0xb8, 0x60000000);
	wait_us(c, 2 * 1000);
	w32(c, base_addr + 0xbc, 0x000aff8d);
	wait_us(c, 2 * 1000);
	w32(c, base_addr + 0xb0, 0x0a11fb89);
	w32(c, base_addr + 0xcc, 0x0a11fb89);
	wait_us(c, 1 * 1000);
	w32(c, base_addr + 0xb8, 0x62000000);
	w32(c, base_addr + 0xd4, 0x62000000);
	wait_us(c, 20 * 1000);
	ready(c, read_addr + 0x08, 0x7fff80, 0xffff);
	ready(c, read_addr + 0x34, 0x7fff80, 0xffff);
	w32(c, base_addr + 0xb8, 0x02000000);
	wait_us(c, 1 * 1000);
	w32(c, base_addr + 0xbc, 0x0008ff87);
	w32(c, 0x9b4, 0xdb6db600);
	w32(c, base_addr + 0x10, 0x02d508c5);
	w32(c, base_addr + 0xbc, 0x0008ff87);
	w32(c, base_addr + 0x60, 0xf0000000);
}
static void
step3(struct dack_context *c, unsigned path, unsigned ic, unsigned qc)
{
	uint32_t base_addr;
	uint32_t read_addr;
	uint32_t temp;

	base_addr = write_base(path);
	read_addr = read_base(path);

	w32(c, base_addr + 0x0c, 0xdff00220);
	w32(c, base_addr + 0x10, 0x02d508c5);
	w32(c, 0x9b4, 0xdb66db00);
	w32(c, base_addr + 0xb0, 0x0a11fb88);
	w32(c, base_addr + 0xbc, 0xc008ff81);
	w32(c, base_addr + 0xc0, 0x0003d208);
	mask32(c, base_addr + 0xbc, 0xf0000000, ic & 0xf);
	mask32(c, base_addr + 0xc0, 0xf, (ic & 0xf0) >> 4);
	w32(c, base_addr + 0xcc, 0x0a11fb88);
	w32(c, base_addr + 0xd8, 0xe008ff81);
	w32(c, base_addr + 0xdc, 0x0003d208);
	mask32(c, base_addr + 0xd8, 0xf0000000, qc & 0xf);
	mask32(c, base_addr + 0xdc, 0xf, (qc & 0xf0) >> 4);
	w32(c, base_addr + 0xb8, 0x60000000);
	wait_us(c, 2 * 1000);
	mask32(c, base_addr + 0xbc, 0xe, 0x6);
	wait_us(c, 2 * 1000);
	w32(c, base_addr + 0xb0, 0x0a11fb89);
	w32(c, base_addr + 0xcc, 0x0a11fb89);
	wait_us(c, 1 * 1000);
	w32(c, base_addr + 0xb8, 0x62000000);
	w32(c, base_addr + 0xd4, 0x62000000);
	wait_us(c, 20 * 1000);
	ready(c, read_addr + 0x24, 0x07f80000, ic);
	ready(c, read_addr + 0x50, 0x07f80000, qc);
	w32(c, base_addr + 0xb8, 0x02000000);
	wait_us(c, 1 * 1000);
	mask32(c, base_addr + 0xbc, 0xe, 0x3);
	w32(c, 0x9b4, 0xdb6db600);

	/* The reference biases the measurement by +16, then removes it. */
	temp = ((c->out->adc[path][0] + 16) & 1023) |
	    (((c->out->adc[path][1] + 16) & 1023) << 10);
	w32(c, base_addr + 0x68, temp);
	w32(c, base_addr + 0x10, 0x02d508c5);
	w32(c, base_addr + 0x60, 0xf0000000);
	{
		int iv = 0, qv = 0;

		stage(c, RTW8822C_DACK_DAC_POST, path);
		measure(c, &iv, &qv);
		if (c->error == 0) {
			c->out->dac_residual[path][0] =
			    (int16_t)signed10((uint32_t)(iv - 16));
			c->out->dac_residual[path][1] =
			    (int16_t)signed10((uint32_t)(qv - 16));
			c->out->dac_measured |= (uint8_t)(1U << path);
		}
	}
}

static int
converged(const int16_t residual[2])
{

	return residual[0] > -5 && residual[0] < 5 &&
	    residual[1] > -5 && residual[1] < 5;
}

static void
adc_snapshot(struct dack_context *c, unsigned path)
{
	uint32_t addresses[] = {
		write_base(path) + 0x68, 0x1c3c, 0x1c24,
		write_base(path) + 0x30, write_base(path) + 0x60
	};
	unsigned i;

	/*
	 * Native diagnostics: five ordinary read-only BB register reads after
	 * the last post measurement. Do not alter its samples or convergence.
	 * A bus error stops all later I/O and remains the returned error.
	 */
	c->out->adc_snapshot_valid = 0;
	c->out->adc_snapshot_path = (uint8_t)path;
	for (i = 0; i < __arraycount(addresses) && c->error == 0; i++)
		c->out->adc_snapshot[i] = rd(c, addresses[i]);
	c->out->adc_snapshot_error = c->error;
	c->out->adc_snapshot_valid = c->error == 0;
}

static void
adc_cal(struct dack_context *c, unsigned path)
{
	unsigned base = write_base(path), sel = path ? 0x80000 : 0xa0000;
	unsigned attempt;
	int iv = 0, qv = 0;

	stage(c, RTW8822C_DACK_ADC_SETUP, path);
	mask32(c, base + 0x30, DACK_BIT(30), 0);
	if (path == 1)
		w32(c, base + 0x30, 0x30db8041);
	w32(c, base + 0x60, 0xf0040ff0);
	w32(c, base + 0x0c, 0xdff00220);
	w32(c, base + 0x10, 0x02dd08c4);
	w32(c, base + 0x0c, 0x10000260);
	write_rf(c, 0, 0, RF_MASK, 0x10000);
	write_rf(c, 1, 0, RF_MASK, 0x10000);
	for (attempt = 0; attempt < 10 && c->error == 0; attempt++) {
		uint32_t compensation;

		c->out->adc_attempts[path] = (uint8_t)(attempt + 1);
		stage(c, RTW8822C_DACK_ADC_PRE, path);
		w32(c, 0x1c3c, sel + 0x8003);
		w32(c, 0x1c24, 0x00010002);
		measure(c, &iv, &qv);
		if (c->error != 0)
			return;
		c->out->adc_before[path][0] = (int16_t)iv;
		c->out->adc_before[path][1] = (int16_t)qv;
		stage(c, RTW8822C_DACK_ADC_PROGRAM, path);
		c->out->adc[path][0] = (uint16_t)((unsigned)-iv & 1023);
		c->out->adc[path][1] = (uint16_t)((unsigned)-qv & 1023);
		compensation = c->out->adc[path][0] |
		    ((uint32_t)c->out->adc[path][1] << 10);
		c->adck[path] = compensation;
		w32(c, base + 0x68, compensation);
		stage(c, RTW8822C_DACK_ADC_POST, path);
		w32(c, 0x1c3c, sel + 0x8103);
		measure(c, &iv, &qv);
		if (c->error != 0)
			return;
		c->out->adc_residual[path][0] = (int16_t)iv;
		c->out->adc_residual[path][1] = (int16_t)qv;
		c->out->adc_measured |= (uint8_t)(1U << path);
		if (attempt == 9 && c->error == 0)
			adc_snapshot(c, path);
		if (c->error == 0 && converged(c->out->adc_residual[path])) {
			c->out->adc_converged |= (uint8_t)(1U << path);
			break;
		}
	}
	if (attempt == 10 && c->error == 0)
		c->out->adc_nonconverged |= (uint8_t)(1U << path);
	w32(c, 0x1c3c, 3);
	w32(c, base + 0x0c, 0x10000260);
	w32(c, base + 0x10, 0x02d508c4);
	write_rf(c, path, 0x8f, DACK_BIT(13), 1);
}

static int
vector(int measured)
{
	int32_t compensation = -(int32_t)measured;

	/* Two offset representations have different midpoints in the hardware. */
	return compensation >= 0 ? 128 + compensation * 12 / 5 :
	    127 - (-compensation) * 12 / 5;
}

static void
step2(struct dack_context *c, unsigned path)
{
	unsigned base = write_base(path);
	int iv = 0, qv = 0, vi, vq;

	mask32(c, base + 0xbc, 0xf0000000, 0);
	mask32(c, base + 0xc0, 0xf, 8);
	mask32(c, base + 0xd8, 0xf0000000, 0);
	mask32(c, base + 0xdc, 0xf, 8);
	w32(c, 0x1b00, 8);
	w8(c, 0x1bcc, 0x3f);
	w32(c, base + 0x0c, 0xdff00220);
	w32(c, base + 0x10, 0x02d508c5);
	w32(c, 0x1c3c, 0x88103);
	measure(c, &iv, &qv);
	if (c->error != 0)
		return;
	vi = vector(iv);
	vq = vector(qv);
	if (vi < 0 || vi > 255 || vq < 0 || vq > 255) {
		c->error = ERANGE;
		return;
	}
	c->out->dac[path][0] = (uint8_t)vi;
	c->out->dac[path][1] = (uint8_t)vq;
}

static void
step4(struct dack_context *c, unsigned path)
{
	unsigned base = write_base(path);

	w32(c, base + 0x68, 0);
	w32(c, base + 0x10, 0x02d508c4);
	mask32(c, base + 0xbc, 1, 0);
	mask32(c, base + 0x30, DACK_BIT(30), 1);
}

int
rtw8822c_dack(struct rtw88_softc *sc, struct rtw8822c_dack_result *out)
{
	static const uint32_t addresses[] = {
		0x180c, 0x1810, 0x410c, 0x4110, 0x1c3c, 0x1c24, 0x1d70, 0x9b4,
		0x1a00, 0x1a14, 0x1d58, 0x1c38, 0x1e24, 0x1e28, 0x1860, 0x4160
	};
	uint32_t backup[__arraycount(addresses)], rf[2];
	struct dack_context c;
	unsigned i, path, attempt;

	if (out == NULL)
		return EINVAL;
	memset(out, 0, sizeof(*out));
	if (sc == NULL || sc->sc_ops == NULL ||
	    sc->sc_ops->read_4 == NULL || sc->sc_ops->write_4 == NULL ||
	    sc->sc_ops->write_1 == NULL || sc->sc_ops->delay_us == NULL)
		return EINVAL;
	if (sc->sc_started || sc->sc_running)
		return EBUSY;
	if (!sc->sc_resources_ready || !sc->sc_powered || !sc->sc_bootstrapping)
		return ENXIO;
	c = (struct dack_context){ .sc = sc, .out = out,
	    .generation = sc->sc_generation };
	stage(&c, RTW8822C_DACK_SNAPSHOT, 2);
	if (canceled(&c))
		return EINTR;
	for (i = 0; i < __arraycount(addresses); i++)
		backup[i] = rd(&c, addresses[i]);
	for (path = 0; path < 2; path++)
		rf[path] = read_rf(&c, path, 0x8f);
	stage(&c, RTW8822C_DACK_BB_SETUP, 2);
	bb_setting(&c);
	for (path = 0; path < 2 && c.error == 0; path++) {
		adc_cal(&c, path);
		for (attempt = 0; attempt < 10 && c.error == 0; attempt++) {
			out->dac_attempts[path] = (uint8_t)(attempt + 1);
			stage(&c, RTW8822C_DACK_DAC_SETUP, path);
			step1(&c, path);
			stage(&c, RTW8822C_DACK_DAC_PRE, path);
			step2(&c, path);
			stage(&c, RTW8822C_DACK_DAC_PROGRAM, path);
			step3(&c, path, out->dac[path][0], out->dac[path][1]);
			if (c.error == 0 && converged(out->dac_residual[path])) {
				out->dac_converged |= (uint8_t)(1U << path);
				break;
			}
		}
		if (attempt == 10 && c.error == 0)
			out->dac_nonconverged |= (uint8_t)(1U << path);
		step4(&c, path);
		if (c.error == 0)
			out->completed_paths |= (uint8_t)(1U << path);
	}
	stage(&c, RTW8822C_DACK_RESTORE, 2);
	w32(&c, 0x1b00, 8);
	mask32(&c, 0x4130, DACK_BIT(30), 1);
	w8(&c, 0x1bcc, 0);
	w32(&c, 0x1b00, 10);
	w8(&c, 0x1bcc, 0);
	for (i = 0; i < __arraycount(addresses); i++)
		w32(&c, addresses[i], backup[i]);
	for (path = 0; path < 2; path++)
		write_rf(&c, path, 0x8f, RF_MASK, rf[path]);
	if (c.error == 0 && canceled(&c))
		c.error = EINTR;
	if (c.error == 0 && out->adc_measured == 3 && out->dac_measured == 3 &&
	    out->completed_paths == 3) {
		out->restored = 1;
		stage(&c, RTW8822C_DACK_DONE, 1);
	} else if (c.error == 0) {
		c.error = EIO;
	}
	if (c.error != 0)
		out->reset_required = (uint8_t)c.mutated;
	return c.error;
}
