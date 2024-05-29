/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2022 V10lator <v10lator@myway.de>                         *
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

#pragma once

#include <wut-fixups.h>

#include <stdbool.h>
#include <stdint.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct ELEMENT ELEMENT;
    struct ELEMENT
    {
        void *content;
        ELEMENT *next;
    };

    typedef struct
    {
        ELEMENT *first;
        ELEMENT *last;
        size_t size;
    } LIST;

    static inline LIST *createList()
    {
        LIST *ret = MEMAllocFromDefaultHeap(sizeof(LIST));
        if(ret != NULL)
            OSBlockSet(ret, 0x00, sizeof(LIST));

        return ret;
    }

    static inline void clearList(LIST *list, bool freeContents)
    {
        ELEMENT *tmp;
        while(list->first != NULL)
        {
            tmp = list->first;
            list->first = tmp->next;
            if(freeContents)
                MEMFreeToDefaultHeap(tmp->content);

            MEMFreeToDefaultHeap(tmp);
        }

        OSBlockSet(&list->last, 0x00, sizeof(LIST) - sizeof(ELEMENT));
    }

    static inline void destroyList(LIST *list, bool freeContents)
    {
        clearList(list, freeContents);
        MEMFreeToDefaultHeap(list);
    }

    static inline bool addToListBeginning(LIST *list, void *content)
    {
        ELEMENT *newElement = MEMAllocFromDefaultHeap(sizeof(LIST));
        if(newElement == NULL)
            return false;

        newElement->content = content;
        newElement->next = list->first;
        list->first = newElement;

        if(list->last == NULL)
            list->last = newElement;

        list->size++;
        return true;
    }

    static inline bool addToListEnd(LIST *list, void *content)
    {
        ELEMENT *newElement = MEMAllocFromDefaultHeap(sizeof(LIST));
        if(newElement == NULL)
            return false;

        newElement->content = content;
        newElement->next = NULL;

        if(list->first == NULL)
            list->last = list->first = newElement;
        else
        {
            list->last->next = newElement;
            list->last = list->last->next;
        }

        list->size++;
        return true;
    }

#define forEachListEntry(x, y) for(ELEMENT *cur = x->first; cur != NULL && (y = cur->content); cur = cur->next)

    static inline void removeFromList(LIST *list, void *content)
    {
        if(list->first == NULL)
            return;

        if(list->first->content == content)
        {
            ELEMENT *tmp = list->first;
            list->first = tmp->next;
            MEMFreeToDefaultHeap(tmp);

            list->size--;
            if(list->size == 0)
                list->last = NULL;

            return;
        }

        ELEMENT *last = list->first;
        for(ELEMENT *cur = list->first->next; cur != NULL; last = cur, cur = cur->next)
        {
            if(cur->content == content)
            {
                last->next = cur->next;
                if(cur == list->last)
                    list->last = last;

                list->size--;
                MEMFreeToDefaultHeap(cur);
                return;
            }
        }
    }

    static inline void *getContent(LIST *list, uint32_t index)
    {
        ELEMENT *entry = list->first;
        for(uint32_t i = 0; i < index && entry != NULL; ++i)
            entry = entry->next;

        return entry == NULL ? NULL : entry->content;
    }

    static inline void removeContent(LIST *list, uint32_t index, bool freeContent)
    {
        ELEMENT *last = list->first;
        ELEMENT *entry = last;
        for(uint32_t i = 0; i < index && entry != NULL; ++i)
        {
            last = entry;
            entry = entry->next;
        }

        if(entry == NULL)
            return;

        if(last == entry)
            list->first = entry->next;
        else
        {
            last->next = entry->next;
            if(entry == list->last)
                list->last = last;
        }

        if(freeContent)
            MEMFreeToDefaultHeap(entry->content);

        MEMFreeToDefaultHeap(entry);

        list->size--;
        if(list->size == 0)
            list->last = NULL;
    }

    static inline void *getAndRemoveFromList(LIST *list, uint32_t index)
    {
        ELEMENT *last = list->first;
        ELEMENT *entry = last;
        for(uint32_t i = 0; i < index && entry != NULL; ++i)
        {
            last = entry;
            entry = entry->next;
        }

        if(entry == NULL)
            return NULL;

        if(last == entry)
            list->first = entry->next;
        else
        {
            last->next = entry->next;
            if(entry == list->last)
                list->last = last;
        }

        void *ret = entry->content;
        MEMFreeToDefaultHeap(entry);

        list->size--;
        if(list->size == 0)
            list->last = NULL;
        return ret;
    }

    static inline void *wrapLastEntry(LIST *list)
    {
        // TODO
        (void)list;
        return NULL;
    }

    static inline void *wrapFirstEntry(LIST *list)
    {
        if(list->first == NULL)
            return NULL;

        if(list->first == list->last)
            return list->first->content;

        list->last->next = list->first;
        list->first = list->first->next;
        list->last = list->last->next;
        list->last->next = NULL;
        return list->last->content;
    }

#define getListSize(x) (x->size)

#ifdef __cplusplus
}
#endif
