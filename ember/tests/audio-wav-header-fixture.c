/* SPDX-License-Identifier: BSD-2-Clause */
/* Run the real WAV header parser of the audio tools on built headers. */
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include <sys/types.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

/* Values of <sys/audioio.h>. */
#define AUDIO_ENCODING_ULAW		1
#define AUDIO_ENCODING_ALAW		2
#define AUDIO_ENCODING_SLINEAR_LE	6
#define AUDIO_ENCODING_ULINEAR_LE	8

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define getle16(v)	__builtin_bswap16(v)
#define getle32(v)	__builtin_bswap32(v)
#else
#define getle16(v)	(v)
#define getle32(v)	(v)
#endif

static int verbose;

#include "audio-wav-types.h"
#include "audio-wav-parse.h"

static unsigned char file[128];
static size_t filelen;
static int failures;

static void
put(const void *p, size_t n)
{

	if (filelen + n > sizeof file)
		abort();
	memcpy(file + filelen, p, n);
	filelen += n;
}

static void
put16(unsigned v)
{
	unsigned char b[2] = { v & 0xff, (v >> 8) & 0xff };

	put(b, sizeof b);
}

static void
put32(unsigned long v)
{

	put16(v & 0xffff);
	put16((v >> 16) & 0xffff);
}

static void
riff(void)
{

	filelen = 0;
	put("RIFF", 4);
	put32(0);		/* the parser ignores the RIFF length */
	put("WAVE", 4);
}

static void
fmt(unsigned tag, unsigned channels, unsigned long rate, unsigned bits,
    unsigned long len)
{

	put("fmt ", 4);
	put32(len);
	put16(tag);
	put16(channels);
	put32(rate);
	put32(rate * channels * bits / 8);
	put16(channels * bits / 8);
	put16(bits);
}

static void
data(unsigned long len)
{
	static const unsigned char zero[16];

	put("data", 4);
	put32(len);
	if (len > sizeof zero)
		abort();
	put(zero, len);
}

struct result {
	ssize_t hdrlen;
	u_int enc, prec, rate, channels;
	off_t datasize;
};

static void
check(const char *name, const struct result *want)
{
	struct result got = { .datasize = -1 };
	void *copy;

	/* An exact heap copy lets a memory checker see reads past the end. */
	if ((copy = malloc(filelen)) == NULL)
		abort();
	memcpy(copy, file, filelen);
	got.hdrlen = audio_wav_parse_hdr(copy, filelen, &got.enc, &got.prec,
	    &got.rate, &got.channels, &got.datasize);
	free(copy);

	if (got.hdrlen != want->hdrlen ||
	    (want->hdrlen > 0 && (got.enc != want->enc ||
	    got.prec != want->prec || got.rate != want->rate ||
	    got.channels != want->channels ||
	    got.datasize != want->datasize))) {
		fprintf(stderr, "FAIL %s: header %ld (want %ld), encoding %u/%u"
		    " bits, %u Hz, %u channels, %ld data bytes\n", name,
		    (long)got.hdrlen, (long)want->hdrlen, got.enc, got.prec,
		    got.rate, got.channels, (long)got.datasize);
		failures++;
	}
}

int
main(void)
{

	/* The canonical 44-byte header written by audiorecord. */
	riff();
	fmt(WAVE_FORMAT_PCM, 2, 48000, 16, 16);
	data(8);
	check("pcm16", &(struct result){ 44, AUDIO_ENCODING_SLINEAR_LE, 16,
	    48000, 2, 8 });

	riff();
	fmt(WAVE_FORMAT_PCM, 1, 8000, 8, 16);
	data(4);
	check("pcm8", &(struct result){ 44, AUDIO_ENCODING_ULINEAR_LE, 8,
	    8000, 1, 4 });

	/* An odd-sized chunk before the data is skipped with its pad byte. */
	riff();
	fmt(WAVE_FORMAT_PCM, 2, 44100, 16, 16);
	put("LIST", 4);
	put32(3);
	put("abc", 4);
	data(8);
	check("list", &(struct result){ 56, AUDIO_ENCODING_SLINEAR_LE, 16,
	    44100, 2, 8 });

	/* WAVE_FORMAT_EXTENSIBLE carries the real tag in its extension. */
	riff();
	fmt(WAVE_FORMAT_EXTENSIBLE, 2, 96000, 24, 40);
	put16(22);
	put16(24);
	put32(3);
	put16(WAVE_FORMAT_PCM);
	put("\0\0\0\0\20\0\200\0\0\252\0\70\233\161", 14);
	data(12);
	check("extensible", &(struct result){ 68, AUDIO_ENCODING_SLINEAR_LE,
	    24, 96000, 2, 12 });

	riff();
	fmt(WAVE_FORMAT_PCM, 2, 48000, 16, 16);
	put("LIST", 4);
	put32(4);
	put("abcd", 4);
	check("nodata", &(struct result){ .hdrlen = AUDIO_EWAVNODATA });

	riff();
	fmt(WAVE_FORMAT_PCM, 2, 48000, 16, 16);
	filelen = 32;
	check("truncated", &(struct result){ .hdrlen = AUDIO_ESHORTHDR });

	riff();
	fmt(WAVE_FORMAT_PCM, 2, 48000, 12, 16);
	data(8);
	check("badbits", &(struct result){ .hdrlen = AUDIO_EWAVBADPCM });

	riff();
	memcpy(file, "RIFX", 4);
	fmt(WAVE_FORMAT_PCM, 2, 48000, 16, 16);
	data(8);
	check("notriff", &(struct result){ .hdrlen = AUDIO_ENOENT });

	if (failures != 0)
		return EXIT_FAILURE;
	printf("audio WAV header: 8 cases passed\n");
	return EXIT_SUCCESS;
}
