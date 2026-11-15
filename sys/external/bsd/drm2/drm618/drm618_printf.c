/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the calls drm_print.c of dist618 (MIT) makes
 * (snprintf, printk, dev_printk with %pV and %ps in the format), no Linux
 * code.
 *
 * Linux style formatting for the 6.18 core: the conversions of the kernel's
 * snprintf plus %pV (a struct va_format: a format and the address of its
 * arguments, formatted in place) and %ps, %pS, %pf, %pB, %pF (a code address:
 * printed in hex, there is no symbol lookup).  NetBSD's printf prints the
 * address of the argument and the letter after the p, so the log of the DRM
 * core was unreadable.  drm_print.c is compiled with snprintf, printk and
 * the aprint_*_dev functions renamed to the ones below.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <linux/printk.h>	/* struct va_format */

int	drm618_vsnprintf(char *, size_t, const char *, va_list);
int	drm618_snprintf(char *, size_t, const char *, ...) __printflike(3, 4);
int	drm618_printk(const char *, ...) __printflike(1, 2);
void	drm618_aprint_normal_dev(device_t, const char *, ...);
void	drm618_aprint_error_dev(device_t, const char *, ...);
void	drm618_aprint_debug_dev(device_t, const char *, ...);

struct fmt_out {
	char	*buf;
	size_t	len;		/* size of buf, 0: count only */
	size_t	pos;		/* characters of the whole text so far */
};

static void
out_put(struct fmt_out *o, const char *s, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++, o->pos++)
		if (o->len != 0 && o->pos < o->len - 1)
			o->buf[o->pos] = s[i];
}

/* One conversion with a spec of ours (a number or a character): through the kernel's snprintf. */
static void
out_spec(struct fmt_out *o, const char *spec, ...)
{
	char tmp[160];
	va_list ap;
	int n;

	va_start(ap, spec);
	n = vsnprintf(tmp, sizeof tmp, spec, ap);
	va_end(ap);
	if (n >= (int)sizeof tmp)
		n = sizeof tmp - 1;
	if (n > 0)
		out_put(o, tmp, (size_t)n);
}

static void fmt_run(struct fmt_out *, const char *, va_list *);

static void
fmt_run(struct fmt_out *o, const char *fmt, va_list *ap)
{
	const char *p, *lit;

	for (p = fmt; *p != '\0';) {
		char flags[8], spec[40];
		int width = -1, prec = -1, lng = 0, nflags = 0;
		char conv;
		bool left;

		if (*p != '%') {
			lit = p;
			while (*p != '\0' && *p != '%')
				p++;
			out_put(o, lit, (size_t)(p - lit));
			continue;
		}
		p++;
		if (*p == '%') {
			out_put(o, "%", 1);
			p++;
			continue;
		}
		left = false;
		while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') {
			if (*p == '-')
				left = true;
			if (nflags < (int)sizeof flags - 1)
				flags[nflags++] = *p;
			p++;
		}
		flags[nflags] = '\0';
		if (*p == '*') {
			width = va_arg(*ap, int);
			if (width < 0) {
				left = true;
				width = -width;
			}
			p++;
		} else if (*p >= '0' && *p <= '9') {
			width = 0;
			while (*p >= '0' && *p <= '9')
				width = width * 10 + (*p++ - '0');
		}
		if (*p == '.') {
			p++;
			prec = 0;
			if (*p == '*') {
				prec = va_arg(*ap, int);
				p++;
			} else
				while (*p >= '0' && *p <= '9')
					prec = prec * 10 + (*p++ - '0');
		}
		while (*p == 'l' || *p == 'h' || *p == 'z' || *p == 't' || *p == 'j') {
			if (*p == 'l' || *p == 'z' || *p == 't' || *p == 'j')
				lng++;
			p++;
		}
		conv = *p;
		if (conv == '\0')
			break;
		p++;
		if (width > 100)
			width = 100;
		switch (conv) {
		case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
			bool sgn = conv == 'd' || conv == 'i';

			/* every integer goes through long long: the same text */
			{
				char w[16] = "", pr[16] = "";

				if (width >= 0)
					snprintf(w, sizeof w, "%d", width);
				if (prec >= 0)
					snprintf(pr, sizeof pr, ".%d", prec);
				snprintf(spec, sizeof spec, "%%%s%s%sll%c",
				    flags, w, pr, conv);
			}
			if (sgn) {
				long long v = lng >= 2 ? va_arg(*ap, long long) :
				    lng == 1 ? va_arg(*ap, long) : va_arg(*ap, int);

				out_spec(o, spec, v);
			} else {
				unsigned long long v = lng >= 2 ?
				    va_arg(*ap, unsigned long long) : lng == 1 ?
				    va_arg(*ap, unsigned long) : va_arg(*ap, unsigned int);

				out_spec(o, spec, v);
			}
			break;
		}
		case 'c': {
			int c = va_arg(*ap, int);
			char ch = (char)c;
			int pad = width > 1 ? width - 1 : 0;

			while (!left && pad-- > 0)
				out_put(o, " ", 1);
			out_put(o, &ch, 1);
			while (left && pad-- > 0)
				out_put(o, " ", 1);
			break;
		}
		case 's': {
			const char *s = va_arg(*ap, const char *);
			size_t n;
			int pad;

			if (s == NULL)
				s = "(null)";
			n = strlen(s);
			if (prec >= 0 && n > (size_t)prec)
				n = (size_t)prec;
			pad = width > (int)n ? width - (int)n : 0;
			while (!left && pad-- > 0)
				out_put(o, " ", 1);
			out_put(o, s, n);
			while (left && pad-- > 0)
				out_put(o, " ", 1);
			break;
		}
		case 'p': {
			char k = *p;

			if (k == 'V') {
				struct va_format *vaf = va_arg(*ap, struct va_format *);

				p++;
				if (vaf != NULL && vaf->fmt != NULL && vaf->va != NULL)
					fmt_run(o, vaf->fmt, vaf->va);
				break;
			}
			if (k == 's' || k == 'S' || k == 'f' || k == 'F' || k == 'B') {
				void *a = va_arg(*ap, void *);

				p++;
				/* a code address: hex, no symbol */
				out_spec(o, "0x%llx", (unsigned long long)(uintptr_t)a);
				break;
			}
			out_spec(o, "0x%llx",
			    (unsigned long long)(uintptr_t)va_arg(*ap, void *));
			break;
		}
		default:
			/* a conversion this formatter does not know: said, not guessed */
			out_put(o, "%?", 2);
			break;
		}
	}
}

int
drm618_vsnprintf(char *buf, size_t len, const char *fmt, va_list ap)
{
	struct fmt_out o;
	va_list ap2;

	o.buf = buf;
	o.len = len;
	o.pos = 0;
	va_copy(ap2, ap);
	fmt_run(&o, fmt, &ap2);
	va_end(ap2);
	if (len != 0)
		buf[o.pos < len - 1 ? o.pos : len - 1] = '\0';
	return (int)o.pos;
}

int
drm618_snprintf(char *buf, size_t len, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = drm618_vsnprintf(buf, len, fmt, ap);
	va_end(ap);
	return n;
}

#define	LOGLINE	512

int
drm618_printk(const char *fmt, ...)
{
	char line[LOGLINE];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = drm618_vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	printf("%s", line);
	return n;
}

void
drm618_aprint_normal_dev(device_t dev, const char *fmt, ...)
{
	char line[LOGLINE];
	va_list ap;

	va_start(ap, fmt);
	drm618_vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	aprint_normal_dev(dev, "%s", line);
}

void
drm618_aprint_error_dev(device_t dev, const char *fmt, ...)
{
	char line[LOGLINE];
	va_list ap;

	va_start(ap, fmt);
	drm618_vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	aprint_error_dev(dev, "%s", line);
}

void
drm618_aprint_debug_dev(device_t dev, const char *fmt, ...)
{
	char line[LOGLINE];
	va_list ap;

	va_start(ap, fmt);
	drm618_vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	aprint_debug_dev(dev, "%s", line);
}
