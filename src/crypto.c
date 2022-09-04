/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 3 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include <wut-fixups.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils.h>

#include <crypto.h>
#include <thread.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include <coreinit/memdefaultheap.h>
#include <coreinit/time.h>

static spinlock rngLock;
static volatile uint32_t entropy;

#define reseed()                      \
    {                                 \
        entropy ^= OSGetSystemTick(); \
        entropy ^= OSGetTick();       \
    }

// Based on George Marsaglias paper "Xorshift RNGs" from https://www.jstatsoft.org/article/view/v008i14
#define rngRun()                      \
    {                                 \
        if(entropy)                   \
        {                             \
            entropy ^= entropy << 13; \
            entropy ^= entropy >> 17; \
            entropy ^= entropy << 5;  \
        }                             \
        else                          \
            reseed();                 \
    }

static OSSL_FUNC_rand_newctx_fn custom_rand_newctx;
static OSSL_FUNC_rand_freectx_fn custom_rand_freectx;
static OSSL_FUNC_rand_instantiate_fn custom_rand_instantiate;
static OSSL_FUNC_rand_uninstantiate_fn custom_rand_uninstantiate;
static OSSL_FUNC_rand_generate_fn custom_rand_generate;
static OSSL_FUNC_rand_gettable_ctx_params_fn custom_rand_gettable_ctx_params;
static OSSL_FUNC_rand_get_ctx_params_fn custom_rand_get_ctx_params;
static OSSL_FUNC_rand_enable_locking_fn custom_rand_enable_locking;

static void *custom_rand_newctx(ossl_unused void *provctx, ossl_unused void *parent, ossl_unused const OSSL_DISPATCH *parent_dispatch)
{
    int *st = MEMAllocFromDefaultHeap(sizeof(*st));
    if(st != NULL)
        *st = EVP_RAND_STATE_UNINITIALISED;

    return st;
}

static void custom_rand_freectx(void *vrng)
{
    MEMFreeToDefaultHeap(vrng);
}

static int custom_rand_instantiate(void *vrng, ossl_unused unsigned int strength, ossl_unused int prediction_resistance, ossl_unused const unsigned char *pstr, ossl_unused size_t pstr_len, ossl_unused const OSSL_PARAM params[])
{
    *(int *)vrng = EVP_RAND_STATE_READY;
    return 1;
}

static int custom_rand_uninstantiate(void *vrng)
{
    *(int *)vrng = EVP_RAND_STATE_UNINITIALISED;
    return 1;
}

int custom_rand_generate(ossl_unused void *vdrbg, unsigned char *out, size_t outlen, ossl_unused unsigned int strength, ossl_unused int prediction_resistance, ossl_unused const unsigned char *adin, ossl_unused size_t adinlen)
{
    --out;
    ++outlen;

    spinLock(rngLock);

    while(--outlen)
    {
        rngRun();
        *++out = entropy;
    }

    spinReleaseLock(rngLock);

    return 1;
}

static int custom_rand_enable_locking(ossl_unused void *vrng)
{
    return 1;
}

static int custom_rand_get_ctx_params(void *vrng, OSSL_PARAM params[])
{
    OSSL_PARAM *p;

    p = OSSL_PARAM_locate(params, OSSL_RAND_PARAM_STATE);
    if(p != NULL && !OSSL_PARAM_set_int(p, *(int *)vrng))
        return 0;

    p = OSSL_PARAM_locate(params, OSSL_RAND_PARAM_STRENGTH);
    if(p != NULL && !OSSL_PARAM_set_int(p, 500))
        return 0;

    p = OSSL_PARAM_locate(params, OSSL_RAND_PARAM_MAX_REQUEST);
    if(p != NULL && !OSSL_PARAM_set_size_t(p, INT_MAX))
        return 0;
    return 1;
}

static const OSSL_PARAM known_gettable_ctx_params[] = {
    OSSL_PARAM_int(OSSL_RAND_PARAM_STATE, NULL),
    OSSL_PARAM_uint(OSSL_RAND_PARAM_STRENGTH, NULL),
    OSSL_PARAM_size_t(OSSL_RAND_PARAM_MAX_REQUEST, NULL),
    OSSL_PARAM_END
};

static const OSSL_PARAM *custom_rand_gettable_ctx_params(ossl_unused void *vrng, ossl_unused void *provctx)
{
    return known_gettable_ctx_params;
}

static const OSSL_DISPATCH custom_rand_functions[] = {
    { OSSL_FUNC_RAND_NEWCTX, (void (*)(void))custom_rand_newctx },
    { OSSL_FUNC_RAND_FREECTX, (void (*)(void))custom_rand_freectx },
    { OSSL_FUNC_RAND_INSTANTIATE, (void (*)(void))custom_rand_instantiate },
    { OSSL_FUNC_RAND_UNINSTANTIATE, (void (*)(void))custom_rand_uninstantiate },
    { OSSL_FUNC_RAND_GENERATE, (void (*)(void))custom_rand_generate },
    { OSSL_FUNC_RAND_ENABLE_LOCKING, (void (*)(void))custom_rand_enable_locking },
    { OSSL_FUNC_RAND_GETTABLE_CTX_PARAMS, (void (*)(void))custom_rand_gettable_ctx_params },
    { OSSL_FUNC_RAND_GET_CTX_PARAMS, (void (*)(void))custom_rand_get_ctx_params },
    { 0, NULL }
};

static const OSSL_ALGORITHM custom_rand_rand[] = {
    { "custom", "provider=custom-rand", custom_rand_functions },
    { NULL, NULL, NULL }
};

static const OSSL_ALGORITHM *custom_rand_query(ossl_unused void *provctx, int operation_id, int *no_cache)
{
    *no_cache = 0;
    return operation_id == OSSL_OP_RAND ? custom_rand_rand : NULL;
}

/* Functions we provide to the core */
static const OSSL_DISPATCH custom_rand_method[] = {
    { OSSL_FUNC_PROVIDER_TEARDOWN, (void (*)(void))OSSL_LIB_CTX_free },
    { OSSL_FUNC_PROVIDER_QUERY_OPERATION, (void (*)(void))custom_rand_query },
    { 0, NULL }
};

static int custom_rand_provider_init(ossl_unused const OSSL_CORE_HANDLE *handle, ossl_unused const OSSL_DISPATCH *in, const OSSL_DISPATCH **out, void **provctx)
{
    *provctx = OSSL_LIB_CTX_new();
    if(*provctx == NULL)
        return 0;

    *out = custom_rand_method;
    return 1;
}

void addEntropy(void *e, size_t l)
{
    uint8_t *buf8 = (uint8_t *)e;
    uint32_t *buf32;
    size_t l32 = l >> 2;
    if(l32)
    {
        buf32 = (uint32_t *)e;
        --buf32;
        buf8 += l32 << 2;
        l %= 4;
    }

    ++l;
    ++l32;
    --buf8;

    spinLock(rngLock);

    while(--l32)
    {
        rngRun();
        entropy ^= *++buf32;
    }

    while(--l)
    {
        rngRun();
        entropy ^= *++buf8;
    }

    spinReleaseLock(rngLock);
}

bool initCrypto()
{
    reseed();
    spinCreateLock(rngLock, SPINLOCK_FREE);
    return OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL) == 1 && OSSL_PROVIDER_add_builtin(NULL, "custom-rand", custom_rand_provider_init) && RAND_set_DRBG_type(NULL, "custom", NULL, NULL, NULL) && OSSL_PROVIDER_try_load(NULL, "custom-rand", 1) != NULL;
}

static inline bool getHash(const void *data, size_t data_len, void *hash, const EVP_MD *type)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ret = false;

    if(ctx)
    {
        if(EVP_DigestInit(ctx, type) == 1)
        {
            if(EVP_DigestUpdate(ctx, data, data_len) == 1)
                ret = EVP_DigestFinal(ctx, hash, NULL) == 1;
        }

        EVP_MD_CTX_free(ctx);
    }

    return ret;
}

bool getMD5(const void *data, size_t data_len, void *hash)
{
    return getHash(data, data_len, hash, EVP_md5());
}

bool getSHA256(const void *data, size_t data_len, void *hash)
{
    return getHash(data, data_len, hash, EVP_sha256());
}

bool encryptAES(void *data, int data_len, const unsigned char *key, const unsigned char *iv, void *encrypted)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    bool ret = false;

    if(ctx)
    {
        if(EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv) == 1)
        {
            int len;
            if(EVP_EncryptUpdate(ctx, encrypted, &len, data, data_len) == 1)
                ret = EVP_EncryptFinal_ex(ctx, encrypted + len, &len) == 1;
        }

        EVP_CIPHER_CTX_free(ctx);
    }

    return ret;
}
