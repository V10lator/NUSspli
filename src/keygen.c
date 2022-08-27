/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2022 V10lator <v10lator@myway.de>                    *
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

#include <openssl/evp.h>

#include <crypto.h>
#include <otp.h>
#include <titles.h>
#include <utils.h>

#include <stdint.h>
#include <string.h>

#include <coreinit/memory.h>

static const uint8_t KEYGEN_SECRET[10] = { 0xfd, 0x04, 0x01, 0x05, 0x06, 0x0b, 0x11, 0x1c, 0x2d, 0x49 };

static inline const char *transformPassword(TITLE_KEY in)
{
    switch(in)
    {
    case TITLE_KEY_mypass:
        return "mypass";
    case TITLE_KEY_nintendo:
        return "nintendo";
    case TITLE_KEY_test:
        return "test";
    case TITLE_KEY_1234567890:
        return "1234567890";
    case TITLE_KEY_Lucy131211:
        return "Lucy131211";
    case TITLE_KEY_fbf10:
        return "fbf10";
    case TITLE_KEY_5678:
        return "5678";
    case TITLE_KEY_1234:
        return "1234";
    case TITLE_KEY_:
        return "";
    default:
        debugPrintf("Unknown password!");
        return "mypass"; // Seems to work so far even for newest releases
    }
}

bool generateKey(const TitleEntry *te, char *out)
{
    const uint8_t *ti = (const uint8_t *)&(te->tid);
    size_t i;
    size_t j;
    switch(getTidHighFromTid(te->tid))
    {
    case TID_HIGH_VWII_IOS:
        ti += 2;
        i = 8 - 3;
        j = 8 - 3 + 10;
        break;
    default:
        i = 8 - 1;
        j = 8 - 1 + 10;
    }

    uint8_t key[17];
    // The salt is a md5 hash of the keygen secret + part of the title key
    OSBlockMove(key, KEYGEN_SECRET, 10, false);
    OSBlockMove(key + 10, ++ti, i, false);
    if(!getMD5(key, j, key))
        return false;

    // The key is the password salted with the md5 hash from above
    const char *pw = transformPassword(te->key);
    debugPrintf("Using password \"%s\"", pw);
    if(PKCS5_PBKDF2_HMAC_SHA1(pw, strlen(pw), key, 16, 20, 16, key) == 0)
        return false;

    // The final key needs to be AES encrypted with the Wii U common key and part of the title ID padded with zeroes as IV
    uint8_t iv[16];
    OSBlockMove(iv, &(te->tid), 8, false);
    OSBlockSet(iv + 8, 0, 8);
    if(!encryptAES(key, 16, getCommonKey(), iv, key))
        return false;

    // Finally we print the key as a hex string to the out arg
    for(i = 0; i < 16; ++i)
    {
        sprintf(out, "%02x", key[i]);
        out += 2;
    }

#ifdef NUSSPLI_DEBUG
    out -= 32;
    debugPrintf("Key: 0x%s", out);
#endif
    return true;
}
