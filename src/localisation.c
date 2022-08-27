/***************************************************************************
 * Copyright (C) 2015 Dimok                                                *
 * Copyright (c) 2022 V10lator <v10lator@myway.de>                         *
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
 * with this program; if not, If not, see <http://www.gnu.org/licenses/>.  *             *
 ***************************************************************************/

#include <wut-fixups.h>

#include <stdbool.h>
#include <string.h>

#include <localisation.h>

#include <file.h>
#include <utils.h>

#include <jansson.h>

#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>

struct MSG;
typedef struct MSG MSG;
struct MSG
{
    uint32_t hash;
    const char *msgstr;
    MSG *next;
};

static MSG *baseMSG = NULL;

#define HASHMULTIPLIER 31 // or 37

/*
 * Hashing function from https://stackoverflow.com/a/2351171
 * The original from "The Practice of Programming" has some hints about collisions, should that ever matter:
 * The hash function returns the result modulo the size of the array. If the hash func-
 * tion distributes key values uniformly, the precise array size doesn't matter. It's hard
 * to be certain that a hash function is dependable, though, and even the best function
 * may have trouble with some input sets, so it's wise to make the array size a prime
 * number to give a bit of extra insurance by guaranteeing that the array size, the hash
 * multiplier, and likely data values have no common factor.
 */
static inline uint32_t hash_string(const unsigned char *str_param)
{
    uint32_t hash = 0;

    while(*str_param != '\0')
        hash = HASHMULTIPLIER * hash + *str_param++;

    return hash;
}

static void addMSG(const char *msgid, const char *msgstr)
{
    if(!msgstr)
        return;

    MSG *msg = (MSG *)MEMAllocFromDefaultHeap(sizeof(MSG));
    msg->hash = hash_string((unsigned char *)msgid);
    msg->msgstr = strdup(msgstr);
    msg->next = NULL;

    if(baseMSG == NULL)
        baseMSG = msg;
    else
    {
        MSG *last = baseMSG;
        while(last->next != NULL)
            last = last->next;

        last->next = msg;
    }

    return;
}

void gettextCleanUp()
{
    while(baseMSG)
    {
        MSG *nextMsg = baseMSG->next;
        MEMFreeToDefaultHeap((void *)(baseMSG->msgstr));
        MEMFreeToDefaultHeap(baseMSG);
        baseMSG = nextMsg;
    }
}

bool gettextLoadLanguage(const char *langFile)
{
    debugPrintf("Loading language file: %s", langFile);

    void *buffer;
    size_t size = readFile(langFile, &buffer);
    if(buffer == NULL)
        return false;

    bool ret = true;
#ifdef NUSSPLI_DEBUG
    json_error_t jerr;
    json_t *json = json_loadb(buffer, size, 0, &jerr);
#else
    json_t *json = json_loadb(buffer, size, 0, NULL);
#endif
    if(json)
    {
        size = json_object_size(json);
        if(size != 0)
        {
            const char *key;
            json_t *value;
            json_object_foreach(json, key, value)
            {
                if(json_is_string(value))
                    addMSG(key, json_string_value(value));
                else
                    debugPrintf("Not a string: %s", key);
            }
        }
        else
        {
            debugPrintf("Error parsing json!");
            ret = false;
        }

        json_decref(json);
    }
    else
    {
        debugPrintf("Error parsing json: %s", jerr.text);
        ret = false;
    }

    MEMFreeToDefaultHeap(buffer);
    return ret;
}

const char *gettext(const char *msgid)
{
    uint32_t hash = hash_string((unsigned char *)msgid);

    for(MSG *msg = baseMSG; msg != NULL; msg = msg->next)
        if(msg->hash == hash)
            return msg->msgstr;

    return msgid;
}
