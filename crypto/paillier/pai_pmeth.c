/* ====================================================================
 * Copyright (c) 2015 - 2017 The GmSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the GmSSL Project.
 *    (http://gmssl.org/)"
 *
 * 4. The name "GmSSL Project" must not be used to endorse or promote
 *    products derived from this software without prior written
 *    permission. For written permission, please contact
 *    guanzhi1980@gmail.com.
 *
 * 5. Products derived from this software may not be called "GmSSL"
 *    nor may "GmSSL" appear in their names without prior written
 *    permission of the GmSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the GmSSL Project
 *    (http://gmssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE GmSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE GmSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 */

#include <stdio.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/paillier.h>
#include "internal/evp_int.h"
#include "pai_lcl.h"

typedef struct {
	int bits;
} PAILLIER_PKEY_CTX;

static int pkey_paillier_init(EVP_PKEY_CTX *ctx)
{
	PAILLIER_PKEY_CTX *dctx;
	if (!(dctx = OPENSSL_zalloc(sizeof(*dctx)))) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_INIT, ERR_R_MALLOC_FAILURE);
		return 0;
	}
	dctx->bits = 4096;
	(void)EVP_PKEY_CTX_set_data(ctx, dctx);
	return 1;
}

static int pkey_paillier_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
{
	PAILLIER_PKEY_CTX *dctx;
	PAILLIER_PKEY_CTX *sctx;
	if (!pkey_paillier_init(dst))
		return 0;
	dctx = EVP_PKEY_CTX_get_data(dst);
	sctx = EVP_PKEY_CTX_get_data(src);
	OPENSSL_assert(sctx);
	*dctx = *sctx;
	return 1;
}

static void pkey_paillier_cleanup(EVP_PKEY_CTX *ctx)
{
	PAILLIER_PKEY_CTX *dctx = EVP_PKEY_CTX_get_data(ctx);
	if (dctx) {
		OPENSSL_free(dctx);
	}
}

static int pkey_paillier_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
	PAILLIER_PKEY_CTX *dctx = EVP_PKEY_CTX_get_data(ctx);
	PAILLIER *pai = NULL;
	if (!(pai = PAILLIER_new())) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_KEYGEN, ERR_R_MALLOC_FAILURE);
		return 0;
	}
	if (!EVP_PKEY_assign_PAILLIER(pkey, pai)) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_KEYGEN, ERR_R_EVP_LIB);
		PAILLIER_free(pai);
		return 0;
	}
	if (!PAILLIER_generate_key(EVP_PKEY_get0_PAILLIER(pkey), dctx->bits)) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_KEYGEN, ERR_R_PAILLIER_LIB);
		return 0;
	}
	return 1;
}

static int pkey_paillier_encrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
	const unsigned char *in, size_t inlen)
{
	int ret = 0;
	PAILLIER *key = EVP_PKEY_get0_PAILLIER(EVP_PKEY_CTX_get0_pkey(ctx));
	BIGNUM *m = NULL;
	BIGNUM *c = NULL;

	if (!out) {
		*outlen = PAILLIER_size(key);
		return 1;
	} else if (*outlen < (size_t)PAILLIER_size(key)) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_ENCRYPT, PAILLIER_R_BUFFER_TOO_SMALL);
		return 0;
	}

	if (!(m = BN_new()) || !(c = BN_new())) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_ENCRYPT, ERR_R_MALLOC_FAILURE);
		goto end;
	}
	if (!BN_bin2bn(in, (int)inlen, m)) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_ENCRYPT, ERR_R_BN_LIB);
		goto end;
	}
	if (!PAILLIER_encrypt(c, m, key)) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_ENCRYPT, ERR_R_PAILLIER_LIB);
		goto end;
	}

	/* the ciphertext has no prefix zeros */
	*outlen = BN_bn2bin(c, out);
	ret = 1;

end:
	BN_clear_free(m);
	BN_free(c);
	return ret;
}

static int pkey_paillier_decrypt(EVP_PKEY_CTX *ctx, unsigned char *out, size_t *outlen,
	const unsigned char *in, size_t inlen)
{
	int ret = 0;
	PAILLIER *key = EVP_PKEY_get0_PAILLIER(EVP_PKEY_CTX_get0_pkey(ctx));
	BIGNUM *m = NULL;
	BIGNUM *c = NULL;

	if (!out) {
		*outlen = PAILLIER_size(key);
		return 1;
	} else if (*outlen < (size_t)PAILLIER_size(key)) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_DECRYPT, PAILLIER_R_BUFFER_TOO_SMALL);
		return 0;
	}

	if (!(m = BN_new()) || !(c = BN_new())) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_DECRYPT, ERR_R_MALLOC_FAILURE);
		goto end;
	}
	if (!BN_bin2bn(in, (int)inlen, c)) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_DECRYPT, ERR_R_BN_LIB);
		goto end;
	}
	if (!PAILLIER_decrypt(m, c, key)) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_DECRYPT, ERR_R_PAILLIER_LIB);
		goto end;
	}

	/* the plaintext has no prefix zeros */
	*outlen = BN_bn2bin(m, out);
	ret = 1;
end:
	BN_free(m);
	BN_free(c);
	return ret;
}

static int pkey_paillier_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
	PAILLIER_PKEY_CTX *dctx = EVP_PKEY_CTX_get_data(ctx);
	switch (type) {
	case EVP_PKEY_CTRL_PAILLIER_KEYGEN_BITS:
		if (p1 < PAILLIER_MIN_KEY_BITS) {
			PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_CTRL, PAILLIER_R_KEY_SIZE_TOO_SMALL);
			return -2;
		}
		dctx->bits = p1;
		return 1;
	}
	return -2;
}

static int pkey_paillier_ctrl_str(EVP_PKEY_CTX *ctx, const char *type, const char *value)
{
	if (!value) {
		PAILLIERerr(PAILLIER_F_PKEY_PAILLIER_CTRL_STR, PAILLIER_R_VALUE_MISSING);
		return 0;
	}
	if (!strcmp(type, "bits")) {
		int nbits = atoi(value);
		return EVP_PKEY_CTX_set_paillier_keygen_bits(ctx, nbits);
	}
	return -2;
}

#define EVP_PKEY_PAILLIER NID_paillier


const EVP_PKEY_METHOD paillier_pkey_meth = {
	EVP_PKEY_PAILLIER,	/* pkey_id */
	0,			/* flags */
	pkey_paillier_init,	/* init */
	pkey_paillier_copy,	/* copy */
	pkey_paillier_cleanup,	/* cleanup */
	NULL,			/* paramgen_init */
	NULL,			/* paramgen */
	NULL,			/* keygen_init */
	pkey_paillier_keygen,	/* keygen */
	NULL,			/* sign_init */
	NULL,			/* sign */
	NULL,			/* verify_init */
	NULL,			/* verify */
	NULL,			/* verify_recover_init */
	NULL,			/* verify_recover */
	NULL,			/* signctx_init */
	NULL,			/* signctx */
	NULL,			/* verifyctx_init */
	NULL,			/* verifyctx */
	NULL,			/* encrypt_init */
	pkey_paillier_encrypt,	/* encrypt */
	NULL,			/* decrypt_init */
	pkey_paillier_decrypt,	/* decrypt */
	NULL,			/* derive_init */
	NULL,			/* derive */
	pkey_paillier_ctrl,	/* ctrl */
	pkey_paillier_ctrl_str	/* ctrl_str */
};
