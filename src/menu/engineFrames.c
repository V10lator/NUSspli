/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2026 V10lator <v10lator@myway.de>                         *
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
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <config.h>
#include <input.h>
#include <localisation.h>
#include <menu/engineFrames.h>
#include <menu/utils.h>
#include <renderer.h>
#include <ui.h>
#include <utils.h>

// The bars chase their target instead of jumping with every sample. A
// finished bar snaps so the last frame of a download cannot be left
// behind, and a target behind the value on screen snaps as well: that is
// a bar starting over - the file bar with its next file - and gliding
// down would drag it backwards through a download that is already done.
// Everything else glides at two bar lengths per second, which turns a
// sampler step into a movement the eye can follow.
static float glideBar(UiAnim *anim, float target)
{
    if(target >= 1.0f || target < anim->value)
    {
        anim->value = target;
        anim->target = target;
        return target;
    }

    uiAnimTo(anim, target);
    return uiAnimStep(anim, 2.0f);
}

void showStatusFrame(const char *line0, const char *line1, bool bar, int logLine)
{
    startNewFrame();
    textToFrame(0, 0, line0);
    if(bar)
        barToFrame(1, 0, 40, 0.0f);
    if(line1 != NULL)
        textToFrame(1, bar ? 41 : 0, line1);
    if(logLine >= 0)
        writeScreenLog(logLine);
    drawFrame();
    showFrame();
}

void drawMcpProgressFrame(void *ctx)
{
    McpProgressView *view = (McpProgressView *)ctx;
    char toScreen[512];

    startNewFrame();
    strcpy(toScreen, localise(view->inst ? "Installing" : "Uninstalling"));
    strcat(toScreen, " ");
    strcat(toScreen, view->game);
    textToFrame(0, 0, toScreen);
    barToFrame(1, 0, 40, glideBar(&view->bar, view->ratio));
    humanize(view->sizeProgress, toScreen);
    strcat(toScreen, " / ");
    humanize(view->sizeTotal, toScreen + strlen(toScreen));
    textToFrame(1, 41, toScreen);

    if(view->sizeProgress != 0)
        textToFrame(1, ALIGNED_RIGHT, view->speed);

    writeScreenLog(2);
}

static void drawStatLine(int line, curl_off_t totalSize, curl_off_t currentSize, float bps, uint32_t *eta, UiAnim *anim)
{
    if(currentSize)
    {
        // The ticket download reports bytes while the total is still unknown
        // (dltotal == 0), and dividing by that would produce an infinity which
        // then overflows the percentage buffer in barToFrame().
        float tmp = 0.0f;
        if(totalSize)
        {
            tmp = currentSize;
            tmp /= totalSize;
        }
        barToFrame(line, 0, 29, glideBar(anim, tmp));
        // A speed at or near zero makes the quotient infinite or larger than
        // *eta can hold, and converting such a float to uint32_t is undefined.
        if(totalSize && bps > 0.0f)
        {
            float secs = (totalSize - currentSize) / bps;
            *eta = secs >= (float)UINT32_MAX ? UINT32_MAX : (uint32_t)secs;
        }
    }
    else
        barToFrame(line, 0, 29, glideBar(anim, 0.0D));

    char toScreen[256];
    humanize(currentSize, toScreen);
    char *ptr = toScreen + strlen(toScreen);
    strcpy(ptr, " / ");
    ptr += 3;
    humanize(totalSize, ptr);
    textToFrame(line, 30, toScreen);

    // UINT32_MAX means there is no usable estimate: either none has been taken
    // yet, or the transfer is too slow to put a bound on.
    if(*eta != UINT32_MAX)
    {
        secsToTime(*eta, toScreen);
        textToFrame(line, ALIGNED_RIGHT, toScreen);
    }
}

void drawDownloadProgressFrame(void *ctx)
{
    DLProgressView *view = (DLProgressView *)ctx;
    downloadData *data = view->data;
    QUEUE_DATA *queueData = view->queueData;
    char toScreen[512];
    int line;

    startNewFrame();

    if(data != NULL)
    {
        if(queueData != NULL)
        {
            sprintf(toScreen, "%s (%d/%d)", data->name, queueData->current, queueData->packages);
            line = textToFrameMultiline(0, ALIGNED_CENTER, toScreen, MAX_CHARS);
        }
        else
            line = textToFrameMultiline(0, ALIGNED_CENTER, data->name, MAX_CHARS);

        drawStatLine(line++, data->dltotal, data->dlnow + view->dlnow, view->bps, &data->eta, &view->bars[0]);

        if(queueData != NULL)
            drawStatLine(line++, queueData->dlSize, queueData->downloaded + view->dlnow, view->bps, &queueData->eta, &view->bars[1]);

        lineToFrame(line++, SCREEN_COLOR_WHITE);

        sprintf(toScreen, "(%d/%d)", data->dcontent + 1, data->contents);
        textToFrame(line, ALIGNED_CENTER, toScreen);
    }
    else
        line = 0;

    if(!view->preparing)
    {
        strcpy(toScreen, localise("Downloading"));
        strcat(toScreen, " ");
        strcat(toScreen, view->name);
        textToFrame(line, 0, toScreen);

        getSpeedString(view->bps, toScreen);
        textToFrame(line, ALIGNED_RIGHT, toScreen);

        drawStatLine(++line, view->dltotal, view->dlnow, view->bps, &view->fileEta, &view->bars[2]);
    }
    else
    {
        strcpy(toScreen, localise("Preparing"));
        strcat(toScreen, " ");
        strcat(toScreen, view->name);
        textToFrame(line++, 0, toScreen);
    }

    writeScreenLog(++line);
}

static char networkRetryText[512];
static char *networkRetryDigit;
static int networkRetryFrames;
static int networkRetrySec;
static bool networkRetryAutoResume;

static void updateNetworkRetry()
{
    if(networkRetryAutoResume)
    {
        int s = networkRetryFrames / FRAMERATE;
        if(s != networkRetrySec)
        {
            // Same arithmetic as the old loop: at the first pass s is 9 and
            // "1" + 9 spells the colon that shows for one frame. The digit
            // lives in the rendered text, so rebuild the frame.
            *networkRetryDigit = '1' + s;
            networkRetrySec = s;
            uiInvalidate();
        }
    }

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        uiSetResult(B_RETURN);
        uiPop();
        return;
    }

    if(vpad.trigger & VPAD_BUTTON_Y)
    {
        uiSetResult(Y_RETRY);
        uiPop();
        return;
    }

    if(networkRetryAutoResume && --networkRetryFrames == 0)
    {
        uiSetResult(Y_RETRY);
        uiPop();
    }
}

static void renderNetworkRetry()
{
    drawErrorFrameContent(networkRetryText, B_RETURN | Y_RETRY);
}

static const UIScreen networkRetryDialog = {
    .name = "network retry dialog",
    .buttons = VPAD_BUTTON_B | VPAD_BUTTON_Y,
    .update = updateNetworkRetry,
    .render = renderNetworkRetry,
};

bool showNetworkError(const char *err)
{
    // The caller composes into FS_MAX_PATH + 64, so the text can be longer
    // than this buffer: cut it instead of running over it.
    snprintf(networkRetryText, sizeof(networkRetryText), "%s", err);
    networkRetryDigit = NULL;
    networkRetryAutoResume = autoResumeEnabled();
    networkRetryFrames = 9 * FRAMERATE; // 9 seconds
    networkRetrySec = 9 * FRAMERATE;

    if(networkRetryAutoResume)
    {
        strcat(networkRetryText, "\n\n");
        networkRetryDigit = networkRetryText + strlen(networkRetryText);
        const char *pt = localise("Next try in _ seconds.");
        strcpy(networkRetryDigit, pt);
        const char *n = strchr(pt, '_');
        networkRetryDigit += n - pt;
    }

    return uiModal(&networkRetryDialog, NULL) == Y_RETRY;
}

static char ticketConfirmTid[17];
static const char *ticketGeneratedDir;

static void updateTicketConfirm()
{
    ErrorOptions pressed = 0;

    if(vpad.trigger & VPAD_BUTTON_A)
        pressed |= A_CONTINUE;
    else if(vpad.trigger & VPAD_BUTTON_B)
        pressed |= B_RETURN;

    if(pressed)
    {
        uiSetResult((int)pressed);
        uiPop();
    }
}

static void renderTicketConfirm()
{
    startNewFrame();
    textToFrame(0, 0, localise("Title ID:"));
    textToFrame(1, 3, ticketConfirmTid);

    int line = MAX_LINES - 1;
    textToFrame(line--, 0, localise("Press " BUTTON_B " to return"));
    textToFrame(line--, 0, localise("Press " BUTTON_A " to continue"));
    lineToFrame(line, SCREEN_COLOR_WHITE);
}

static const UIScreen ticketConfirmDialog = {
    .name = "ticket confirm dialog",
    .buttons = VPAD_BUTTON_A | VPAD_BUTTON_B,
    .update = updateTicketConfirm,
    .render = renderTicketConfirm,
};

ErrorOptions showTicketConfirmDialog(uint64_t titleID)
{
    hex(titleID, 16, ticketConfirmTid);
    return (ErrorOptions)uiModal(&ticketConfirmDialog, NULL);
}

static void updateTicketGenerated()
{
    if(vpad.trigger)
    {
        uiSetResult(1);
        uiPop();
    }
}

static void renderTicketGenerated()
{
    colorStartNewFrame(SCREEN_COLOR_D_GREEN);
    textToFrame(0, 0, localise("Fake ticket generated on:"));
    textToFrame(1, 0, prettyDir(ticketGeneratedDir));
    textToFrame(3, 0, localise("Press any key to return"));
}

static const UIScreen ticketGeneratedDialog = {
    .name = "ticket generated dialog",
    .buttons = 0, // any press dismisses the dialog
    .update = updateTicketGenerated,
    .render = renderTicketGenerated,
};

void showTicketGeneratedDialog(const char *dir)
{
    ticketGeneratedDir = dir;
    uiModal(&ticketGeneratedDialog, NULL);
}
