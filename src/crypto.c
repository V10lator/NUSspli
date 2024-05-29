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

#include <crypto.h>
#include <thread.h>

#include <mbedtls/aes.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/time.h>
#pragma GCC diagnostic pop

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

int NUSrng(void *data, unsigned char *out, size_t outlen)
{
    (void)data;

    --out;
    ++outlen;

    spinLock(rngLock);

    while(--outlen)
    {
        rngRun();
        *++out = entropy;
    }

    spinReleaseLock(rngLock);

    return 0;
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
        entropy ^= *++buf32; // Not uninitialised!
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
    return true;
}

bool encryptAES(void *data, int data_len, const unsigned char *key, unsigned char *iv, void *encrypted)
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    bool ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, data_len, iv, data, encrypted) == 0;
    /*
     * We're not calling mbedtls_aes_free() as at the time of writing
     * all it does is overwriting the mbedtls_aes_context struct with
     * zeros.
     * As game key calculation isn't top secret we don't need this.
     *
     * TODO: Check the codes at every mbed TLS update to make sure
     * we won't need to call it.
     */
    // mbedtls_aes_free(&ctx);
    return ret;
}
