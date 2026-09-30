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

#pragma once

#include <wut-fixups.h>

#include <stdbool.h>
#include <stdint.h>

// Frame rate of the whole loop. The display refreshes at 60 Hz and
// drawFrame() waits one interrupt per frame at 60 and two at 30 before it
// presents, so that wait sets the pace; the swap itself runs at a vblank
// anyway as the renderer is created with SDL_RENDERER_PRESENTVSYNC. Frame
// based timers (the download redraw throttle, the countdown in
// showNetworkError) count in these units, and everything that waits
// instead of presenting follows it too: UI_WAIT_TICKS in ui.c (uiYield)
// and the frame rate asked of the software keyboard in input.c.
#define FRAMERATE            30

#define DPAD_COOLDOWN_FRAMES (FRAMERATE / 2) // half a second

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * The UI kernel.
     *
     * There is one main loop (uiRun) instead of a while loop per menu. Every
     * screen is a pair of update (input and state) and render (drawing) and
     * lives on a stack. Navigation happens by pushing screens; modal dialogs
     * block their caller through uiModal, which runs the very same frame.
     *
     * Boot is the first screen of that loop instead of a pyramid of calls
     * before it: its steps run one per frame, so the loading screen has the
     * same frame cadence as every other screen and can animate one day.
     *
     * The engine must not include renderer.h. It talks to the UI through this
     * header only: uiWaitWhile for blocking work (with an optional frame
     * callback implemented on the UI side), uiShowOverlay/uiHideOverlay for
     * breadcrumbs and uiPostError for errors raised on other threads.
     */

    typedef struct UIScreen
    {
        const char *name;

        // The buttons this screen reacts to: a press of one of them rebuilds
        // the picture, which is where the old loops set redraw = true after
        // every trigger input. Zero (the default) means "may react to
        // anything", so every press redraws - a dialog dismissing on any
        // button declares nothing and a screen forgetting the field only
        // costs a redraw, it cannot show a stale frame.
        uint32_t buttons;
        void (*enter)(void *param); // Called on push
        void (*update)(void); // Input and state, once per frame, required
        void (*render)(void); // Fills the frame when dirty, NULL keeps the picture below
        void (*leave)(void); // Called on pop
    } UIScreen;

    // Optional per-frame callback for uiWaitWhile: draws and handles input
    // while the engine is waiting. Must be implemented on the UI side.
    typedef void (*UiWaitFrame)(void *ctx);

    // The main loop. Runs until uiExit or until the app has to stop.
    void uiRun() __attribute__((__hot__));
    void uiExit();

    // One frame: app messages, input, update, render when the frame is
    // dirty, present, VSync. Also used by the modal pumps below, so every
    // frame goes through here.
    void uiFrame() __attribute__((__hot__));

    void uiPush(const UIScreen *screen, void *param);
    void uiPop();
    bool uiTopIs(const UIScreen *screen);

    // Blocks the caller until the pushed screen pops itself again and
    // returns the result set via uiSetResult. This is how dialogs keep the
    // control flow of their caller without owning a loop each.
    int uiModal(const UIScreen *screen, void *param);
    void uiSetResult(int result);

    // Marks the picture as out of date: the next uiFrame rebuilds it
    // instead of only presenting the retained one. A trigger input the top
    // screen declares (see UIScreen.buttons), push and the end of an engine
    // flow invalidate automatically, pop only while no engine flow owns the
    // frame (see uiPump); content that changes beyond that (the D-pad
    // repeat blocks, the network retry countdown) calls this where the old
    // loops set redraw = true.
    void uiInvalidate();

    // Animation clock: frames since the loop started (a screen can snapshot
    // it in enter to know its own age) and the milliseconds between the
    // previous and the current frame. The delta is clamped to a few frame
    // durations, so a stall (a blocking call, a dialog that stayed open)
    // slows motion down instead of throwing it across the screen.
    uint32_t uiFrameCount();
    uint32_t uiDeltaMs();

    // A float that glides towards its target at rate units per second,
    // independent of FRAMERATE. The caller writes the target (directly or
    // through uiAnimTo), draws value and advances it once per frame with
    // uiAnimStep, which returns it. Both fields start at zero, which is
    // also what a zeroed or static struct holds.
    typedef struct UiAnim
    {
        float value;
        float target;
    } UiAnim;

    void uiAnimTo(UiAnim *anim, float target);
    float uiAnimStep(UiAnim *anim, float rate);

    // Blocks until any button has been pressed (pumping frames).
    void uiWaitKey();

    // Blocks while *condition is true, pumping one frame per iteration.
    // frame may be NULL: the current picture is simply presented (VSync
    // paced) until the condition clears. This is the helper for an engine
    // wait that outlives a single frame so the UI keeps running.
    void uiWaitWhile(volatile bool *condition, UiWaitFrame frame, void *ctx);

    // Puts the caller to sleep for one frame without presenting, see
    // UI_WAIT_TICKS. Nothing is shown here: the callers wait inside the
    // I/O queue, where checkForQueueErrors ends the wait on its own, and
    // a dialog belongs to a frame (see uiDrainEvents).
    void uiYield();

    // Breadcrumbs: overlays are drawn on top of the retained frame.
    void *uiShowOverlay(const char *text);
    void uiHideOverlay(void *overlay);
    // Presents the retained frame, overlays included, and returns. Not a pump:
    // no events, no input, no transition step, so it is safe to call from a
    // wait loop that owns its own condition. A breadcrumb only becomes
    // visible in a presented frame, which is why a caller that shows one and
    // then waits has to present here instead of only sleeping. Returns at once,
    // so the wait it sits in stays a wait and not a stall.
    void uiPresentFrame();

    // Thread-safe and one slot deep: hands the text over to the thread
    // running uiRun(), which shows the dialog in one of its next frames.
    // The caller neither draws nor waits for the user, so an error raised
    // deep inside a worker cannot wedge the loop that would have to show
    // it - and a message posted by the loop thread itself is left for the
    // frame it stands in instead of waiting for itself. A second message
    // while one is still waiting is dropped: the first one is fatal too
    // and two dialogs for one broken queue help nobody.
    void uiPostError(const char *text);

    // Shows a message waiting in the queue (see uiPostError) and leaves
    // the app afterwards - posted errors are unrecoverable. Every frame
    // calls this before it runs the top screen, so no thread identity is
    // needed to know who may draw: the frame functions are only called
    // from the main thread, everything else posts. main() calls it once
    // more after uiRun() returned, because the tear down after the loop
    // still writes files and has no frame left of its own.
    void uiDrainEvents();

    // One frame for engine code that cannot draw itself: drain posted
    // errors, read input, let the UI side fill the frame through the
    // optional callback and present it. frame == NULL keeps the retained
    // picture and just paces the loop. The first
    // pump of a flow marks the frame as engine owned until the flow is back
    // in the frame that started it, so a dialog popping in between does not
    // flash the stale screen below it.
    void uiPump(UiWaitFrame frame, void *ctx);

    // Wrappers for the software keyboard, which owns the screen while it
    // is up and pauses the regular frame flow around it.
    void uiPauseRenderer();
    void uiResumeRenderer();

    // Draws the goodbye frame on the power button exit path.
    void uiDrawByeFrame();

#ifdef __cplusplus
}
#endif
