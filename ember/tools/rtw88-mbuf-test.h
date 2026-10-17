/* SPDX-License-Identifier: BSD-2-Clause */
/* Native allocation API doubles for the actual driver helper. */
#ifndef RTW88_MBUF_TEST_H
#define RTW88_MBUF_TEST_H
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define M_DONTWAIT 0
#define MT_DATA 1
#define M_EXT 1
/* EMBER64 AArch64 GCC16 header layout: MSIZE=512. */
#define MHLEN 400
#define MLEN 456
#define MINCLSIZE (MHLEN + MLEN + 1)
#define MCLBYTES 2048
#define __UNCONST(p) ((void *)(uintptr_t)(const void *)(p))
#define mtod(m, t) ((t)(m)->m_data)
#define uimin(a, b) ((a) < (b) ? (a) : (b))
struct ifnet { int identity; };
struct mbuf {
	struct mbuf *m_next;
	char *m_data;
	int m_len, m_flags;
	size_t external_size;
	struct { int len; struct ifnet *rcvif; } m_pkthdr;
	char storage[MLEN];
};
struct mbuf *m_gethdr(int, int);
struct mbuf *m_get(int, int);
struct mbuf *m_free(struct mbuf *);
void m_freem(struct mbuf *);
void m_set_rcvif(struct mbuf *, struct ifnet *);
void test_external(struct mbuf *, size_t, int);
#define MCLGET(m, how) test_external((m), MCLBYTES, (how))
#define MEXTMALLOC(m, size, how) test_external((m), (size), (how))
struct mbuf *m_devget(char *, int, int, struct ifnet *);
#endif
