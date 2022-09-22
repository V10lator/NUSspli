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

#include <file.h>
#include <filesystem.h>
#include <utils.h>

#include <coreinit/filesystem_fsa.h>

#include <stdbool.h>

static FSAClientHandle handle;
static NUSDEV usb;

bool initFS()
{
    if(dirExists(NUSDIR_USB1 "usr"))
        usb = NUSDEV_USB01;
    else if(dirExists(NUSDIR_USB2 "usr"))
        usb = NUSDEV_USB02;
    else
        usb = NUSDEV_NONE;

    if(FSAInit() == FS_ERROR_OK)
    {
        handle = FSAAddClient(NULL);
        if(handle)
        {
            FSError err = FSAMount(handle, "/vol/external01", "/vol/app_sd", FSA_MOUNT_FLAG_BIND_MOUNT, NULL, 0);
            if(err == FS_ERROR_OK)
                return true;

            FSADelClient(handle);
        }

        FSAShutdown();
    }

    return false;
}

void deinitFS()
{
    FSAUnmount(handle, "/vol/app_sd", FSA_UNMOUNT_FLAG_BIND_MOUNT);
    FSADelClient(handle);
    FSAShutdown();
}

NUSDEV getUSB()
{
    return usb;
}
