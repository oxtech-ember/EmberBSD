/* SPDX-License-Identifier: BSD-2-Clause */
/* Verify the SDK's attached DER CMS with one directly issued release signer. */
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/cms.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include "verify.h"

int
eu_digest(const void *data, size_t n, char result[65])
{
	static const char hex[] = "0123456789abcdef";
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned len, i;

	if (EVP_Digest(data, n, digest, &len, EVP_sha256(), NULL) != 1 || len != 32)
		return -1;
	for (i = 0; i < len; i++) {
		result[2 * i] = hex[digest[i] >> 4];
		result[2 * i + 1] = hex[digest[i] & 15];
	}
	result[64] = 0;
	return 0;
}

static int
leaf_ok(X509 *leaf)
{
	BASIC_CONSTRAINTS *bc;
	EXTENDED_KEY_USAGE *eku;
	int i, code = 0, mail = 0, ok;

	bc = X509_get_ext_d2i(leaf, NID_basic_constraints, NULL, NULL);
	eku = X509_get_ext_d2i(leaf, NID_ext_key_usage, NULL, NULL);
	ok = bc != NULL && !bc->ca && bc->pathlen == NULL &&
	     X509_get_key_usage(leaf) == KU_DIGITAL_SIGNATURE && eku != NULL &&
	     sk_ASN1_OBJECT_num(eku) == 2;
	if (ok) {
		for (i = 0; i < 2; i++) {
			int nid = OBJ_obj2nid(sk_ASN1_OBJECT_value(eku, i));
			if (nid == NID_code_sign)
				code++;
			if (nid == NID_email_protect)
				mail++;
		}
		ok = code == 1 && mail == 1;
	}
	BASIC_CONSTRAINTS_free(bc);
	EXTENDED_KEY_USAGE_free(eku);
	return ok;
}

struct eu_json *
eu_cms_json(const void *data, size_t n, const char *anchor)
{
	const unsigned char *p = data;
	STACK_OF(CMS_SignerInfo) * infos;
	STACK_OF(X509) *signers = NULL;
	X509_ALGOR *digest = NULL;
	X509_VERIFY_PARAM *vp;
	struct eu_json *json = NULL;
	CMS_ContentInfo *cms = NULL;
	X509_STORE *store = NULL;
	BIO *out = NULL;
	char *body;
	long size;

	if (data == NULL || anchor == NULL || n == 0 || n > EU_MAX_CMS)
		goto done;
	cms = d2i_CMS_ContentInfo(NULL, &p, (long)n);
	if (cms == NULL || p != (const unsigned char *)data + n ||
	    OBJ_obj2nid(CMS_get0_type(cms)) != NID_pkcs7_signed ||
	    CMS_is_detached(cms) != 0 || CMS_get0_eContentType(cms) == NULL ||
	    OBJ_obj2nid(CMS_get0_eContentType(cms)) != NID_pkcs7_data)
		goto done;
	infos = CMS_get0_SignerInfos(cms);
	if (infos == NULL || sk_CMS_SignerInfo_num(infos) != 1)
		goto done;
	CMS_SignerInfo_get0_algs(sk_CMS_SignerInfo_value(infos, 0), NULL, NULL, &digest,
				 NULL);
	if (digest == NULL || OBJ_obj2nid(digest->algorithm) != NID_sha256)
		goto done;
	store = X509_STORE_new();
	out = BIO_new(BIO_s_mem());
	if (store == NULL || out == NULL ||
	    X509_STORE_load_locations(store, anchor, NULL) != 1)
		goto done;
	vp = X509_STORE_get0_param(store);
	X509_VERIFY_PARAM_set_depth(vp, 0);
	X509_VERIFY_PARAM_set_auth_level(vp, 2);
	if (X509_VERIFY_PARAM_set_purpose(vp, X509_PURPOSE_SMIME_SIGN) != 1 ||
	    X509_VERIFY_PARAM_set_flags(vp, X509_V_FLAG_X509_STRICT |
						X509_V_FLAG_CHECK_SS_SIGNATURE) != 1 ||
	    CMS_verify(cms, NULL, store, NULL, out, CMS_BINARY) != 1)
		goto done;
	signers = CMS_get0_signers(cms);
	if (signers == NULL || sk_X509_num(signers) != 1 ||
	    !leaf_ok(sk_X509_value(signers, 0)))
		goto done;
	size = BIO_get_mem_data(out, &body);
	if (size > 0 && size <= EU_MAX_CONTENT)
		json = eu_json_parse(body, (size_t)size);
done:
	sk_X509_free(signers);
	BIO_free(out);
	X509_STORE_free(store);
	CMS_ContentInfo_free(cms);
	if (json == NULL)
		errno = EINVAL;
	return json;
}

int
file_sha(const char *path, char result[65])
{
	unsigned char chunk[65536], digest[32];
	unsigned int len = 0;
	struct stat st;
	int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0)
		return -1;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	int ok = -1;
	if (!ctx || fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) ||
	    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
		goto done;
	ssize_t n;
	while ((n = read(fd, chunk, sizeof(chunk))) != 0) {
		if (n < 0) {
			if (errno == EINTR)
				continue;
			goto done;
		}
		if (EVP_DigestUpdate(ctx, chunk, (size_t)n) != 1)
			goto done;
	}
	if (EVP_DigestFinal_ex(ctx, digest, &len) != 1 || len != 32)
		goto done;
	for (unsigned i = 0; i < 32; i++)
		snprintf(result + i * 2, 3, "%02x", digest[i]);
	ok = 0;
done:
	close(fd);
	EVP_MD_CTX_free(ctx);
	return ok;
}
