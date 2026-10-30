/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The firmware console as a tty.  Output and input go through the SBI
 * console calls, input is polled from a callout.  Good enough for a
 * board whose UART has no driver yet.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD$");

#include <sys/param.h>
#include <sys/callout.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/kauth.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/tty.h>

#include <dev/cons.h>
#include <dev/fdt/fdtvar.h>

#include <machine/machdep.h>

struct sbitty_softc {
	device_t	sc_dev;
	struct tty	*sc_tty;
	callout_t	sc_ch;
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "esp,uart0" },
	{ .compat = "esp,esp32s3-uart" },
	{ .compat = "ns16550a" },	/* QEMU virt, when com is left out */
	DEVICE_COMPAT_EOL
};

static int	sbitty_match(device_t, cfdata_t, void *);
static void	sbitty_attach(device_t, device_t, void *);

CFATTACH_DECL_NEW(sbitty, sizeof(struct sbitty_softc),
    sbitty_match, sbitty_attach, NULL, NULL);

extern struct cfdriver sbitty_cd;

static dev_type_open(sbitty_open);
static dev_type_close(sbitty_close);
static dev_type_read(sbitty_read);
static dev_type_write(sbitty_write);
static dev_type_ioctl(sbitty_ioctl);
static dev_type_stop(sbitty_stop);
static dev_type_tty(sbitty_tty);
static dev_type_poll(sbitty_poll);

const struct cdevsw sbitty_cdevsw = {
	.d_open = sbitty_open,
	.d_close = sbitty_close,
	.d_read = sbitty_read,
	.d_write = sbitty_write,
	.d_ioctl = sbitty_ioctl,
	.d_stop = sbitty_stop,
	.d_tty = sbitty_tty,
	.d_poll = sbitty_poll,
	.d_mmap = nommap,
	.d_kqfilter = ttykqfilter,
	.d_discard = nodiscard,
	.d_flag = D_TTY
};

static void
sbitty_cnputc(dev_t dev, int c)
{
	uartputc(c);
}

static int
sbitty_cngetc(dev_t dev)
{
	return uartgetc();
}

static struct consdev sbitty_cons = {
	.cn_putc = sbitty_cnputc,
	.cn_getc = sbitty_cngetc,
	.cn_pollc = nullcnpollc,
	.cn_dev = NODEV,
	.cn_pri = CN_NORMAL,
};

static int
sbitty_console_match(int phandle)
{
	return of_compatible_match(phandle, compat_data);
}

static void
sbitty_console_consinit(struct fdt_attach_args *faa, u_int uart_freq)
{
	cn_tab = &sbitty_cons;
}

static const struct fdt_console sbitty_console = {
	.match = sbitty_console_match,
	.consinit = sbitty_console_consinit,
};

FDT_CONSOLE(sbitty, &sbitty_console);

static int
sbitty_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	/* One instance: the firmware has one console. */
	if (sbitty_cons.cn_dev != NODEV)
		return 0;
	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sbitty_attach(device_t parent, device_t self, void *aux)
{
	struct sbitty_softc * const sc = device_private(self);

	sc->sc_dev = self;
	callout_init(&sc->sc_ch, 0);

	sbitty_cons.cn_dev = makedev(cdevsw_lookup_major(&sbitty_cdevsw),
	    device_unit(self));
	cn_tab = &sbitty_cons;

	aprint_naive("\n");
	aprint_normal(": firmware console\n");
}

static void
sbitty_start(struct tty *tp)
{
	const int s = spltty();

	if (tp->t_state & (TS_TTSTOP | TS_BUSY)) {
		splx(s);
		return;
	}
	ttypull(tp);
	tp->t_state |= TS_BUSY;
	while (tp->t_outq.c_cc != 0)
		uartputc(getc(&tp->t_outq));
	tp->t_state &= ~TS_BUSY;
	splx(s);
}

static int
sbitty_param(struct tty *tp, struct termios *t)
{
	return 0;
}

static void
sbitty_poll_input(void *v)
{
	struct sbitty_softc * const sc = v;
	struct tty * const tp = sc->sc_tty;
	int c;

	while ((c = uartgetc()) >= 0) {
		if (tp->t_state & TS_ISOPEN)
			(*tp->t_linesw->l_rint)(c, tp);
	}
	callout_reset(&sc->sc_ch, 1, sbitty_poll_input, sc);
}

static int
sbitty_open(dev_t dev, int flag, int mode, struct lwp *l)
{
	struct sbitty_softc * const sc =
	    device_lookup_private(&sbitty_cd, minor(dev));
	struct tty *tp;
	bool first = false;

	if (sc == NULL)
		return ENXIO;

	const int s = spltty();
	if (sc->sc_tty == NULL) {
		sc->sc_tty = tty_alloc();
		tty_attach(sc->sc_tty);
	}
	tp = sc->sc_tty;
	tp->t_oproc = sbitty_start;
	tp->t_param = sbitty_param;
	tp->t_dev = dev;

	if (kauth_authorize_device_tty(l->l_cred, KAUTH_DEVICE_TTY_OPEN, tp)) {
		splx(s);
		return EBUSY;
	}

	if ((tp->t_state & TS_ISOPEN) == 0) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG | CLOCAL;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = 115200;
		ttsetwater(tp);
		first = true;
	}
	tp->t_state |= TS_CARR_ON;
	splx(s);

	const int error = (*tp->t_linesw->l_open)(dev, tp);
	if (error == 0 && first)
		sbitty_poll_input(sc);
	return error;
}

static int
sbitty_close(dev_t dev, int flag, int mode, struct lwp *l)
{
	struct sbitty_softc * const sc =
	    device_lookup_private(&sbitty_cd, minor(dev));
	struct tty * const tp = sc->sc_tty;

	callout_stop(&sc->sc_ch);
	(*tp->t_linesw->l_close)(tp, flag);
	ttyclose(tp);
	return 0;
}

static int
sbitty_read(dev_t dev, struct uio *uio, int flag)
{
	struct tty * const tp = sbitty_tty(dev);

	return (*tp->t_linesw->l_read)(tp, uio, flag);
}

static int
sbitty_write(dev_t dev, struct uio *uio, int flag)
{
	struct tty * const tp = sbitty_tty(dev);

	return (*tp->t_linesw->l_write)(tp, uio, flag);
}

static int
sbitty_poll(dev_t dev, int events, struct lwp *l)
{
	struct tty * const tp = sbitty_tty(dev);

	return (*tp->t_linesw->l_poll)(tp, events, l);
}

static int
sbitty_ioctl(dev_t dev, u_long cmd, void *data, int flag, struct lwp *l)
{
	struct tty * const tp = sbitty_tty(dev);
	int error;

	error = (*tp->t_linesw->l_ioctl)(tp, cmd, data, flag, l);
	if (error != EPASSTHROUGH)
		return error;
	return ttioctl(tp, cmd, data, flag, l);
}

static void
sbitty_stop(struct tty *tp, int flag)
{
}

static struct tty *
sbitty_tty(dev_t dev)
{
	struct sbitty_softc * const sc =
	    device_lookup_private(&sbitty_cd, minor(dev));

	return sc != NULL ? sc->sc_tty : NULL;
}
