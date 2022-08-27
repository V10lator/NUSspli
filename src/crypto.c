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

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

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

int osslBytes(unsigned char *buf, int num)
{
    --buf;
    ++num;

    spinLock(rngLock);

    while(--num)
    {
        rngRun();
        *++buf = entropy;
    }

    spinReleaseLock(rngLock);

    return 1;
}

static void osslCleanup()
{
    // STUB
}

static int osslStatus()
{
    return 1;
}

static const RAND_METHOD srm = {
    .seed = NULL,
    .bytes = osslBytes,
    .cleanup = osslCleanup,
    .add = NULL,
    .pseudorand = osslBytes,
    .status = osslStatus,
};

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
    return OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL) == 1 && RAND_set_rand_method(&srm) == 1;
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
