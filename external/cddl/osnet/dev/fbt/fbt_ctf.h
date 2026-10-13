/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD (AI-assisted), bounded CTF2/3 decoding for FBT. */
#ifndef _FBT_CTF_H_
#define _FBT_CTF_H_

/* The caller supplies sys/ctf.h, fixed-width types and memcpy(). */
struct fbt_ctf_type {
	uint32_t name, kind, vlen, ref;
	uint64_t size;
	size_t hdrlen, reclen;
};

static inline size_t
fbt_ctf_width(unsigned int ctf_version)
{
	return ctf_version == CTF_VERSION_3 ? 4 : 2;
}

static inline uint32_t
fbt_ctf_word(const void *p, size_t width)
{
	uint16_t v2;
	uint32_t v3;

	if (width == 2) {
		memcpy(&v2, p, sizeof(v2));
		return v2;
	}
	memcpy(&v3, p, sizeof(v3));
	return v3;
}

static inline uint32_t
fbt_ctf_kind(unsigned int ctf_version, uint32_t info)
{
	return ctf_version == CTF_VERSION_3 ? CTF_V3_INFO_KIND(info) :
	    CTF_V2_INFO_KIND(info);
}

static inline uint32_t
fbt_ctf_vlen(unsigned int ctf_version, uint32_t info)
{
	return ctf_version == CTF_VERSION_3 ? CTF_V3_INFO_VLEN(info) :
	    CTF_V2_INFO_VLEN(info);
}

/* Decode one native-endian record without reading past the type section. */
static inline int
fbt_ctf_decode(unsigned int ctf_version, const uint8_t *p, size_t len,
    struct fbt_ctf_type *t)
{
	size_t width, payload = 0, elem = 0;
	uint32_t info, sentinel;
	uint64_t threshold;

	if (ctf_version != CTF_VERSION_2 && ctf_version != CTF_VERSION_3)
		return -1;
	width = fbt_ctf_width(ctf_version);
	t->hdrlen = 4 + 2 * width;
	if (len < t->hdrlen)
		return -1;
	t->name = fbt_ctf_word(p, 4);
	info = fbt_ctf_word(p + 4, width);
	t->kind = fbt_ctf_kind(ctf_version, info);
	t->vlen = fbt_ctf_vlen(ctf_version, info);
	t->ref = fbt_ctf_word(p + 4 + width, width);
	t->size = t->ref;
	sentinel = ctf_version == CTF_VERSION_3 ? CTF_V3_LSIZE_SENT :
	    CTF_V2_LSIZE_SENT;
	if (t->size == sentinel) {
		if (len - t->hdrlen < 8)
			return -1;
		t->size = (uint64_t)fbt_ctf_word(p + t->hdrlen, 4) << 32 |
		    fbt_ctf_word(p + t->hdrlen + 4, 4);
		t->hdrlen += 8;
	}
	threshold = ctf_version == CTF_VERSION_3 ? CTF_V3_LSTRUCT_THRESH :
	    CTF_V2_LSTRUCT_THRESH;
	switch (t->kind) {
	case CTF_K_INTEGER:
	case CTF_K_FLOAT:
		payload = 4;
		break;
	case CTF_K_ARRAY:
		payload = 2 * width + 4;
		break;
	case CTF_K_FUNCTION:
		/* v2 aligns its 16-bit argument list to four bytes. */
		payload = width * (t->vlen + (width == 2 ? t->vlen & 1 : 0));
		break;
	case CTF_K_STRUCT:
	case CTF_K_UNION:
		elem = t->size < threshold ? (width == 2 ? 8 : 12) : 16;
		break;
	case CTF_K_ENUM:
		elem = 8;
		break;
	case CTF_K_UNKNOWN:
	case CTF_K_FORWARD:
	case CTF_K_POINTER:
	case CTF_K_TYPEDEF:
	case CTF_K_VOLATILE:
	case CTF_K_CONST:
	case CTF_K_RESTRICT:
		break;
	default:
		return -1;
	}
	if (elem != 0) {
		if (t->vlen > (len - t->hdrlen) / elem)
			return -1;
		payload = elem * t->vlen;
	}
	if (payload > len - t->hdrlen)
		return -1;
	t->reclen = t->hdrlen + payload;
	return 0;
}
#endif
