/*	$NetBSD$ */

/*-
 * Copyright (c) 2026 EmberBSD Project
 *
 * Register setup follows the ES8311 programming sequence published by
 * Espressif in esp-adf/components/esp_codec_dev/device/es8311 (Apache-2.0).
 * This driver implements the BSD audio DAI interface and only advertises the
 * clock/format combination used by the Mosaico board.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/audioio.h>

#include <dev/audio/audio_dai.h>
#include <dev/i2c/i2cvar.h>
#include <dev/fdt/fdtvar.h>

#define ES8311_RESET		0x00
#define ES8311_CLK1		0x01
#define ES8311_CLK2		0x02
#define ES8311_CLK3		0x03
#define ES8311_CLK4		0x04
#define ES8311_CLK5		0x05
#define ES8311_CLK6		0x06
#define ES8311_CLK7		0x07
#define ES8311_CLK8		0x08
#define ES8311_DAC_IFACE	0x09
#define ES8311_ADC_IFACE	0x0a
#define ES8311_SYS0B		0x0b
#define ES8311_SYS0C		0x0c
#define ES8311_SYS0D		0x0d
#define ES8311_SYS0E		0x0e
#define ES8311_SYS10		0x10
#define ES8311_SYS11		0x11
#define ES8311_SYS12		0x12
#define ES8311_SYS13		0x13
#define ES8311_SYS14		0x14
#define ES8311_ADC15		0x15
#define ES8311_ADC16		0x16
#define ES8311_ADC17		0x17
#define ES8311_ADC1B		0x1b
#define ES8311_ADC1C		0x1c
#define ES8311_DAC31		0x31
#define ES8311_DAC32		0x32
#define ES8311_DAC37		0x37
#define ES8311_GPIO44		0x44

#define ES8311_DAC_MUTE		0x60
#define ES8311_DAC_UNMUTE	0x00
#define ES8311_DAC_LEVEL_MAX	0xbf
#define ES8311_DAC_LEVEL_DEFAULT	0x80
#define ES8311_BCLK_DIV_4	0x03

enum es8311_mixer {
	ES8311_MIXER_OUTPUTS,
	ES8311_MIXER_MASTER,
	ES8311_MIXER_LAST
};

static const struct device_compatible_entry es8311_compat[] = {
	{ .compat = "everest,es8311" },
	DEVICE_COMPAT_EOL
};

struct es8311_softc {
	device_t		 sc_dev;
	i2c_tag_t		 sc_i2c;
	i2c_addr_t		 sc_addr;
	struct audio_dai_device sc_dai;
	u_int			 sc_mclk;
	u_int			 sc_dai_polarity;
	uint8_t			 sc_dac_level;
	bool			 sc_dai_format_set;
	bool			 sc_initialized;
	bool			 sc_configured;
	int			 sc_mode;
	audio_params_t		 sc_play_params;
	audio_params_t		 sc_record_params;
};

static int	es8311_match(device_t, cfdata_t, void *);
static void	es8311_attach(device_t, device_t, void *);
static int	es8311_open(void *, int);
static int	es8311_set_port(void *, mixer_ctrl_t *);
static int	es8311_get_port(void *, mixer_ctrl_t *);
static int	es8311_query_devinfo(void *, mixer_devinfo_t *);
static int	es8311_initialize(struct es8311_softc *);
static int	es8311_apply_format(struct es8311_softc *);

static uint8_t
es8311_level_to_reg(u_int level)
{
	return (uint8_t)((level * ES8311_DAC_LEVEL_MAX + 127) / 255);
}

static u_int
es8311_reg_to_level(uint8_t reg)
{
	return (u_int)((reg * 255 + ES8311_DAC_LEVEL_MAX / 2) /
	    ES8311_DAC_LEVEL_MAX);
}

CFATTACH_DECL_NEW(es8311ac, sizeof(struct es8311_softc),
	 es8311_match, es8311_attach, NULL, NULL);

static int
es8311_write(struct es8311_softc *sc, uint8_t reg, uint8_t value)
{
	uint8_t data[2] = { reg, value };
	int error;

	error = iic_acquire_bus(sc->sc_i2c, 0);
	if (error != 0)
		return error;
	error = iic_exec(sc->sc_i2c, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    NULL, 0, data, sizeof(data), 0);
	iic_release_bus(sc->sc_i2c, 0);
	return error;
}

static int
es8311_update(struct es8311_softc *sc, uint8_t reg, uint8_t clear,
    uint8_t set)
{
	uint8_t value;
	int error;

	error = iic_acquire_bus(sc->sc_i2c, 0);
	if (error != 0)
		return error;
	error = iic_exec(sc->sc_i2c, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &reg, sizeof(reg), &value, sizeof(value), 0);
	if (error == 0) {
		value = (value & ~clear) | set;
		error = iic_exec(sc->sc_i2c, I2C_OP_WRITE_WITH_STOP,
		    sc->sc_addr, NULL, 0, (uint8_t[]){ reg, value }, 2, 0);
	}
	iic_release_bus(sc->sc_i2c, 0);
	return error;
}

static int
es8311_set_sysclk(audio_dai_tag_t dai, u_int rate, int dir)
{
	struct es8311_softc *sc = audio_dai_private(dai);

	if (dir != AUDIO_DAI_CLOCK_IN || rate != 12288000)
		return EINVAL;
	sc->sc_mclk = rate;
	return 0;
}

static int
es8311_set_dai_format(audio_dai_tag_t dai, u_int format)
{
	struct es8311_softc *sc = audio_dai_private(dai);
	const u_int fmt = __SHIFTOUT(format, AUDIO_DAI_FORMAT_MASK);
	const u_int clk = __SHIFTOUT(format, AUDIO_DAI_CLOCK_MASK);
	const u_int pol = __SHIFTOUT(format, AUDIO_DAI_POLARITY_MASK);

	if (fmt != AUDIO_DAI_FORMAT_I2S ||
	    clk != AUDIO_DAI_CLOCK_CBS_CFS ||
	    pol != AUDIO_DAI_POLARITY_NB_NF)
		return EINVAL;
	sc->sc_dai_polarity = pol;
	sc->sc_dai_format_set = true;
	return 0;
}

static int
es8311_query_format(void *priv, audio_format_query_t *afp)
{
	static const struct audio_format format = {
		.mode = AUMODE_RECORD | AUMODE_PLAY,
		.encoding = AUDIO_ENCODING_SLINEAR_LE,
		.validbits = 16,
		.precision = 16,
		.channels = 1,
		.channel_mask = AUFMT_MONAURAL,
		.frequency_type = 1,
		.frequency = { 48000 },
	};

	(void)priv;
	return audio_query_format(&format, 1, afp);
}

static int
es8311_open(void *priv, int flags)
{
	struct es8311_softc *sc = priv;

	(void)flags;
	/*
	 * audio.c calls open with the CPU DAI's interrupt lock held.  All I2C
	 * setup is done by set_format from thread context instead.
	 */
	return sc->sc_configured ? 0 : EIO;
}

static int
es8311_initialize(struct es8311_softc *sc)
{
	static const struct { uint8_t reg, value; } init[] = {
		{ ES8311_CLK1, 0x30 },
		{ ES8311_CLK2, 0x00 },
		{ ES8311_CLK3, 0x10 },
		{ ES8311_ADC16, 0x24 },
		{ ES8311_CLK4, 0x10 },
		{ ES8311_CLK5, 0x00 },
		{ ES8311_SYS0B, 0x00 },
		{ ES8311_SYS0C, 0x00 },
		{ ES8311_SYS10, 0x1f },
		{ ES8311_SYS11, 0x7f },
		{ ES8311_RESET, 0x80 },
		/* IDF configures the device as a slave after reset. */
		{ ES8311_RESET, 0x80 },
		{ ES8311_CLK1, 0x3f },
		{ ES8311_CLK6, 0x00 },
		{ ES8311_SYS13, 0x10 },
		{ ES8311_ADC1B, 0x0a },
		{ ES8311_ADC1C, 0x6a },
		/* The board BSP sets no_dac_ref: keep GPIO44 on the default path. */
		{ ES8311_GPIO44, 0x08 },
		{ ES8311_DAC31, ES8311_DAC_MUTE },
	};
	size_t n;
	int error, retry_error;

	if (sc->sc_initialized)
		return 0;

	/* ADF repeats this first transaction because it can be lost on I2C. */
	error = es8311_write(sc, ES8311_GPIO44, 0x08);
	retry_error = es8311_write(sc, ES8311_GPIO44, 0x08);
	if (error != 0 && retry_error != 0) {
		aprint_error_dev(sc->sc_dev,
		    "codec GPIO initialization failed after retry: %d\n",
		    retry_error);
		return retry_error;
	}
	if (error != 0)
		aprint_debug_dev(sc->sc_dev,
		    "codec first GPIO write failed; retry succeeded\n");
	if (retry_error != 0)
		aprint_debug_dev(sc->sc_dev,
		    "codec duplicate GPIO write failed; first write succeeded\n");

	for (n = 0; n < __arraycount(init); n++) {
		error = es8311_write(sc, init[n].reg, init[n].value);
		if (error != 0) {
			aprint_error_dev(sc->sc_dev,
			    "codec initialization failed at register 0x%02x: %d\n",
			    init[n].reg, error);
			sc->sc_initialized = false;
			return error;
		}
	}
	sc->sc_initialized = true;
	return 0;
}

static bool
es8311_params_supported(const audio_params_t *p)
{
	return p != NULL && p->sample_rate == 48000 && p->channels == 1 &&
	    p->precision == 16 && p->validbits == 16 &&
	    p->encoding == AUDIO_ENCODING_SLINEAR_LE;
}

static int
es8311_apply_format(struct es8311_softc *sc)
{
	int error;

	if (sc->sc_mode == 0 || !sc->sc_initialized ||
	    !sc->sc_dai_format_set || sc->sc_mclk != 12288000)
		return EINVAL;
	if ((sc->sc_mode & AUMODE_PLAY) != 0 &&
	    !es8311_params_supported(&sc->sc_play_params))
		return EINVAL;
	if ((sc->sc_mode & AUMODE_RECORD) != 0 &&
	    !es8311_params_supported(&sc->sc_record_params))
		return EINVAL;

	error = es8311_write(sc, ES8311_CLK1, 0x3f);
	if (error == 0)
		error = es8311_write(sc, ES8311_CLK2, 0x00);
	if (error == 0)
		error = es8311_write(sc, ES8311_CLK3, 0x10);
	if (error == 0)
		error = es8311_write(sc, ES8311_CLK4, 0x10);
	if (error == 0)
		error = es8311_write(sc, ES8311_CLK5, 0x00);
	if (error == 0)
		error = es8311_write(sc, ES8311_CLK6,
		    sc->sc_dai_polarity == AUDIO_DAI_POLARITY_IB_NF ?
		    ES8311_BCLK_DIV_4 | 0x20 : ES8311_BCLK_DIV_4);
	if (error == 0)
		error = es8311_write(sc, ES8311_CLK7, 0x00);
	if (error == 0)
		error = es8311_write(sc, ES8311_CLK8, 0xff);
	if (error == 0)
		error = es8311_update(sc, ES8311_DAC_IFACE, 0x7c, 0x0c);
	if (error == 0)
		error = es8311_update(sc, ES8311_ADC_IFACE, 0x7c, 0x0c);
	if (error == 0)
		error = es8311_write(sc, ES8311_ADC15, 0x40);
	if (error == 0)
		error = es8311_write(sc, ES8311_ADC16, 0x24);
	if (error == 0)
		error = es8311_write(sc, ES8311_ADC17, 0xbf);
	if (error == 0)
		error = es8311_write(sc, ES8311_SYS0B, 0x00);
	if (error == 0)
		error = es8311_write(sc, ES8311_SYS0C, 0x00);
	if (error == 0)
		error = es8311_write(sc, ES8311_SYS0D, 0x01);
	if (error == 0)
		error = es8311_write(sc, ES8311_SYS0E, 0x02);
	if (error == 0)
		error = es8311_write(sc, ES8311_SYS10, 0x1f);
	if (error == 0)
		error = es8311_write(sc, ES8311_SYS11, 0x7f);
	if (error == 0)
		error = es8311_write(sc, ES8311_SYS12, 0x00);
	if (error == 0)
		error = es8311_write(sc, ES8311_SYS13, 0x10);
	if (error == 0)
		error = es8311_write(sc, ES8311_SYS14, 0x1a);
	if (error == 0)
		error = es8311_write(sc, ES8311_DAC37, 0x08);
	if (error == 0)
		error = es8311_write(sc, ES8311_DAC32, sc->sc_dac_level);
	if (error == 0)
		error = es8311_write(sc, ES8311_DAC31,
		    (sc->sc_mode & AUMODE_PLAY) != 0 ?
		    ES8311_DAC_UNMUTE : ES8311_DAC_MUTE);
	sc->sc_configured = error == 0;
	return error;
}

static int
es8311_set_format(void *priv, int mode, const audio_params_t *play,
    const audio_params_t *record, audio_filter_reg_t *pfil,
    audio_filter_reg_t *rfil)
{
	struct es8311_softc *sc = priv;

	(void)pfil;
	(void)rfil;
	if (mode == 0 || (mode & ~(AUMODE_PLAY | AUMODE_RECORD)) != 0)
		return EINVAL;
	if ((mode & AUMODE_PLAY) != 0 && !es8311_params_supported(play))
		return EINVAL;
	if ((mode & AUMODE_RECORD) != 0 && !es8311_params_supported(record))
		return EINVAL;

	/* A partial I2C transaction must not leave an old format usable. */
	sc->sc_configured = false;
	sc->sc_mode = mode;
	if ((mode & AUMODE_PLAY) != 0)
		sc->sc_play_params = *play;
	if ((mode & AUMODE_RECORD) != 0)
		sc->sc_record_params = *record;
	if (!sc->sc_initialized) {
		int error = es8311_initialize(sc);
		if (error != 0)
			return error;
	}
	return es8311_apply_format(sc);
}

static int
es8311_trigger_output(void *priv, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, const audio_params_t *params)
{
	struct es8311_softc *sc = priv;

	(void)start;
	(void)end;
	(void)blksize;
	(void)intr;
	(void)intrarg;
	(void)params;
	if (!sc->sc_configured)
		return EIO;
	/* audio.c invokes this with its IPL_VM interrupt lock held. */
	return 0;
}

static int
es8311_halt_output(void *priv)
{
	(void)priv;
	/* The auxiliary amplifier is disabled before the CPU DAI is halted. */
	return 0;
}

static void
es8311_close(void *priv)
{
	struct es8311_softc *sc = priv;

	/* close is called with the interrupt lock held; do not access I2C here. */
	(void)sc;
}

static int
es8311_set_port(void *priv, mixer_ctrl_t *mc)
{
	struct es8311_softc *sc = priv;
	u_int left;
	uint8_t level;
	int error;

	if (mc->dev != ES8311_MIXER_MASTER)
		return ENXIO;
	if (mc->type != AUDIO_MIXER_VALUE || mc->un.value.num_channels != 1)
		return EINVAL;
	left = (u_int)mc->un.value.level[0];
	if (left > 255)
		return EINVAL;

	level = es8311_level_to_reg(left);
	if (sc->sc_initialized) {
		error = es8311_write(sc, ES8311_DAC32, level);
		if (error != 0)
			return error;
	}
	sc->sc_dac_level = level;
	return 0;
}

static int
es8311_get_port(void *priv, mixer_ctrl_t *mc)
{
	struct es8311_softc *sc = priv;
	u_int level;

	if (mc->dev != ES8311_MIXER_MASTER)
		return ENXIO;
	if (mc->type != AUDIO_MIXER_VALUE || mc->un.value.num_channels != 1)
		return EINVAL;
	level = es8311_reg_to_level(sc->sc_dac_level);
	mc->un.value.level[0] = level;
	return 0;
}

static int
es8311_query_devinfo(void *priv, mixer_devinfo_t *di)
{
	(void)priv;
	switch (di->index) {
	case ES8311_MIXER_OUTPUTS:
		di->type = AUDIO_MIXER_CLASS;
		strlcpy(di->label.name, AudioCoutputs,
		    sizeof(di->label.name));
		di->mixer_class = ES8311_MIXER_OUTPUTS;
		di->prev = di->next = AUDIO_MIXER_LAST;
		return 0;
	case ES8311_MIXER_MASTER:
		di->type = AUDIO_MIXER_VALUE;
		strlcpy(di->label.name, AudioNmaster,
		    sizeof(di->label.name));
		di->mixer_class = ES8311_MIXER_OUTPUTS;
		di->prev = di->next = AUDIO_MIXER_LAST;
		di->un.v.num_channels = 1;
		di->un.v.delta = 1;
		strlcpy(di->un.v.units.name, AudioNvolume,
		    sizeof(di->un.v.units.name));
		return 0;
	default:
		return ENXIO;
	}
}

static const struct audio_hw_if es8311_hw_if = {
	.open = es8311_open,
	.close = es8311_close,
	.query_format = es8311_query_format,
	.set_format = es8311_set_format,
	.set_port = es8311_set_port,
	.get_port = es8311_get_port,
	.query_devinfo = es8311_query_devinfo,
	.trigger_output = es8311_trigger_output,
	.halt_output = es8311_halt_output,
};

static audio_dai_tag_t
es8311_get_tag(device_t dev, const void *data, size_t len)
{
	struct es8311_softc *sc = device_private(dev);

	return len == sizeof(uint32_t) ? &sc->sc_dai : NULL;
}

static const struct fdtbus_dai_controller_func es8311_dai_funcs = {
	.get_tag = es8311_get_tag,
};

static int
es8311_match(device_t parent, cfdata_t cf, void *aux)
{
	struct i2c_attach_args *ia = aux;
	int match_result;

	if (iic_use_direct_match(ia, cf, es8311_compat, &match_result))
		return match_result;
	return 0;
}

static void
es8311_attach(device_t parent, device_t self, void *aux)
{
	struct es8311_softc *sc = device_private(self);
	struct i2c_attach_args *ia = aux;
	const int phandle = ia->ia_cookie;

	sc->sc_dev = self;
	sc->sc_i2c = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_dac_level = ES8311_DAC_LEVEL_DEFAULT;

	aprint_naive("\n");
	aprint_normal(": Everest ES8311 audio codec\n");

	sc->sc_dai.dai_set_sysclk = es8311_set_sysclk;
	sc->sc_dai.dai_set_format = es8311_set_dai_format;
	sc->sc_dai.dai_hw_if = &es8311_hw_if;
	sc->sc_dai.dai_dev = self;
	sc->sc_dai.dai_priv = sc;
	fdtbus_register_dai_controller(self, phandle, &es8311_dai_funcs);
}
