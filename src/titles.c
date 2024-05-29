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
#include <string.h>

#include <gtitles.h>
#include <menu/utils.h>
#include <titles.h>
#include <utils.h>

const TitleEntry *getTitleEntryByTid(uint64_t tid)
{
    TITLE_CATEGORY cat;

    switch(getTidHighFromTid(tid))
    {
        case TID_HIGH_GAME:
            cat = TITLE_CATEGORY_GAME;
            break;
        case TID_HIGH_UPDATE:
            cat = TITLE_CATEGORY_UPDATE;
            break;
        case TID_HIGH_DLC:
            cat = TITLE_CATEGORY_DLC;
            break;
        case TID_HIGH_DEMO:
            cat = TITLE_CATEGORY_DEMO;
            break;
        default:
            cat = TITLE_CATEGORY_ALL;
    }

    const TitleEntry *haystack = getTitleEntries(cat);
    size_t haySize = getTitleEntriesSize(cat);

    for(++haySize; --haySize; ++haystack)
        if(haystack->tid == tid)
            return haystack;

    if(cat == TITLE_CATEGORY_GAME)
    {
        haystack = getTitleEntries(TITLE_CATEGORY_DISC);
        haySize = getTitleEntriesSize(TITLE_CATEGORY_DISC);

        for(++haySize; --haySize; ++haystack)
            if(haystack->tid == tid)
                return haystack;
    }

    return NULL;
}

const char *tid2name(const char *tid)
{
    uint64_t rtid;
    hexToByte(tid, (uint8_t *)&rtid);
    const TitleEntry *e = getTitleEntryByTid(rtid);
    return e == NULL ? "UNKNOWN" : e->name;
}

bool name2tid(const char *name, char *out)
{
    size_t lower = 0;
    size_t upper = getTitleEntriesSize(TITLE_CATEGORY_ALL);
    size_t current = upper >> 1;
    int strret;

    const TitleEntry *titleEntry = getTitleEntries(TITLE_CATEGORY_ALL);
    while(lower != upper)
    {
        strret = strcmp(titleEntry[current].name, name);
        if(strret == 0)
        {
            hex(titleEntry[current].tid, 16, out);
            return true;
        }

        if(strret < 0)
            upper = current;
        else
            lower = current;

        current = ((upper - lower) >> 1) + lower;
    }

    return false;
}
