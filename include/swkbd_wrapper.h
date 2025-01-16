/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2020-2021 V10lator <v10lator@myway.de>                    *
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

#include <stdint.h>

#include <input.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <nn/swkbd.h>
#pragma GCC diagnostic pop

#ifndef __cplusplus
typedef __uint_least16_t char16_t;
#else
#include <string>
extern "C"
{
#endif

// Enums copied from https://github.com/devkitPro/wut/blob/master/include/nn/swkbd/swkbd_cpp.h
typedef enum
{
    Swkbd_LanguageType__Japanese = 0,
    Swkbd_LanguageType__English = 1,
    Swkbd_LanguageType__French = 2,
    Swkbd_LanguageType__German = 3,
    Swkbd_LanguageType__Italian = 4,
    Swkbd_LanguageType__Spanish = 5,
    Swkbd_LanguageType__Chinese1 = 6,
    Swkbd_LanguageType__Korean = 7,
    Swkbd_LanguageType__Dutch = 8,
    Swkbd_LanguageType__Portuguese = 9,
    Swkbd_LanguageType__Russian = 10,
    Swkbd_LanguageType__Chinese2 = 11,
    Swkbd_LanguageType__Invalid = 12,
    // We add new languages here which can only be used as menu languages
    Swkbd_LanguageType__Portuguese_BR = 13,
    Swkbd_LanguageType__Turkish = 14,
    Swkbd_LanguageType__Welsh = 15,
} Swkbd_LanguageType;

typedef enum
{
    Swkbd_RegionType__Japan = 0,
    Swkbd_RegionType__USA = 1,
    Swkbd_RegionType__Europe = 2,
    Swkbd_RegionType__China = 3,
    Swkbd_RegionType__Korea = 4,
    Swkbd_RegionType__Taiwan = 5,
} Swkbd_RegionType;

typedef enum
{
    Swkbd_LanguageType2__Japanese = 0,
    Swkbd_LanguageType2__English = 1,
    Swkbd_LanguageType2__French = 6,
    Swkbd_LanguageType2__German = 7,
    Swkbd_LanguageType2__Italian = 8,
    Swkbd_LanguageType2__Spanish = 9,
    Swkbd_LanguageType2__Dutch = 10,
    Swkbd_LanguageType2__Portuguese = 11,
    Swkbd_LanguageType2__Russian = 12,
} Swkbd_LanguageType2;

typedef enum
{
    Swkbd_PW_mode__None = 0,
    Swkbd_PW_mode__Hide = 1,
    Swkbd_PW_mode__Fade = 2,
} Swkbd_PW_mode;

// Structs also from the link above
typedef struct
{
    Swkbd_LanguageType languageType;
    ControllerType controllerType;
    KeyboardLayout keyboardMode;
    uint32_t accessFlags;
    Swkbd_LanguageType2 languageType2;
    int32_t unk_0x14;
    bool unk_0x18;
    char16_t *str;
    char16_t numpadCharLeft;
    char16_t numpadCharRight;
    bool showWordSuggestions;
    WUT_PADDING_BYTES(3);
    uint8_t unk_0x28;
    uint8_t unk_0x29;
    uint8_t unk_0x2A;
    bool disableNewLine;
    WUT_UNKNOWN_BYTES(0x9C - 0x2C);
    uint32_t framerate;
    bool showCursor;
    int32_t unk_0xA4;
} Swkbd_ConfigArg;
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x00, languageType);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x04, controllerType);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x08, keyboardMode);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x0C, accessFlags);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x10, languageType2);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x14, unk_0x14);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x18, unk_0x18);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x1C, str);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x20, numpadCharLeft);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x22, numpadCharRight);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x24, showWordSuggestions);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x28, unk_0x28);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x29, unk_0x29);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x2A, unk_0x2A);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x2B, disableNewLine);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0x9C, framerate);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0xA0, showCursor);
WUT_CHECK_OFFSET(Swkbd_ConfigArg, 0xA4, unk_0xA4);
WUT_CHECK_SIZE(Swkbd_ConfigArg, 0xA8);

typedef struct
{
    uint32_t unk_0x00;
    uint32_t unk_0x04;
    uint32_t unk_0x08;
    int32_t unk_0x0C;
    uint32_t unk_0x10;
    int32_t unk_0x14;
} Swkbd_ReceiverArg;
WUT_CHECK_OFFSET(Swkbd_ReceiverArg, 0x00, unk_0x00);
WUT_CHECK_OFFSET(Swkbd_ReceiverArg, 0x04, unk_0x04);
WUT_CHECK_OFFSET(Swkbd_ReceiverArg, 0x08, unk_0x08);
WUT_CHECK_OFFSET(Swkbd_ReceiverArg, 0x0C, unk_0x0C);
WUT_CHECK_OFFSET(Swkbd_ReceiverArg, 0x10, unk_0x10);
WUT_CHECK_OFFSET(Swkbd_ReceiverArg, 0x14, unk_0x14);
WUT_CHECK_SIZE(Swkbd_ReceiverArg, 0x18);

typedef struct
{
    Swkbd_ConfigArg configArg;
    Swkbd_ReceiverArg receiverArg;
} Swkbd_KeyboardArg;
WUT_CHECK_SIZE(Swkbd_KeyboardArg, 0xC0);

typedef struct
{
    KeyboardType type;
    int32_t unk_0x04;
    const char16_t *initialText;
    const char16_t *hintText;
    int32_t maxTextLength;
    Swkbd_PW_mode pwMode;
    uint32_t unk_0x18;
    bool drawInput0Cursor;
    bool higlightInitialText;
    bool showCopyPasteButtons;
    WUT_PADDING_BYTES(1);
} Swkbd_InputFormArg;
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x00, type);
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x04, unk_0x04);
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x08, initialText);
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x0C, hintText);
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x10, maxTextLength);
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x14, pwMode);
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x18, unk_0x18);
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x1C, drawInput0Cursor);
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x1D, higlightInitialText);
WUT_CHECK_OFFSET(Swkbd_InputFormArg, 0x1E, showCopyPasteButtons);
WUT_CHECK_SIZE(Swkbd_InputFormArg, 0x20);

typedef struct
{
    Swkbd_KeyboardArg keyboardArg;
    Swkbd_InputFormArg inputFormArg;
} Swkbd_AppearArg;
WUT_CHECK_SIZE(Swkbd_AppearArg, 0xE0);

typedef struct
{
    void *workMemory;
    Swkbd_RegionType regionType;
    uint32_t unk_0x08;
    FSClient *fsClient;
} Swkbd_CreateArg;
WUT_CHECK_OFFSET(Swkbd_CreateArg, 0x00, workMemory);
WUT_CHECK_OFFSET(Swkbd_CreateArg, 0x04, regionType);
WUT_CHECK_OFFSET(Swkbd_CreateArg, 0x08, unk_0x08);
WUT_CHECK_OFFSET(Swkbd_CreateArg, 0x0C, fsClient);
WUT_CHECK_SIZE(Swkbd_CreateArg, 0x10);

typedef struct
{
    const VPADStatus *vpad;
    const KPADStatus *kpad[4];
} Swkbd_ControllerInfo;
WUT_CHECK_OFFSET(Swkbd_ControllerInfo, 0x00, vpad);
WUT_CHECK_OFFSET(Swkbd_ControllerInfo, 0x04, kpad);
WUT_CHECK_SIZE(Swkbd_ControllerInfo, 0x14);

// Wrapper functions
uint32_t Swkbd_GetWorkMemorySize(uint32_t unk);
bool Swkbd_AppearInputForm(const Swkbd_AppearArg *args);
bool Swkbd_Create(const Swkbd_CreateArg *args);
void Swkbd_SetEnableOkButton(bool enable);
char *Swkbd_GetInputFormString();
const char16_t *Swkbd_GetInputFormString16();
void Swkbd_SetInputFormString(const char *str);
void Swkbd_SetInputFormString16(const char16_t *str);
void Swkbd_Calc(const Swkbd_ControllerInfo *controllerInfo);
bool Swkbd_IsNeedCalcSubThreadFont();
void Swkbd_CalcSubThreadFont();
bool Swkbd_IsNeedCalcSubThreadPredict();
void Swkbd_CalcSubThreadPredict();
void Swkbd_DrawTV();
void Swkbd_DrawDRC();
bool Swkbd_IsDecideOkButton(bool *outIsSelected);
bool Swkbd_IsDecideCancelButton(bool *outIsSelected);
bool Swkbd_DisappearInputForm();
void Swkbd_Destroy();
bool Swkbd_IsHidden();
bool Swkbd_IsReady();

#ifdef __cplusplus
}
#endif
