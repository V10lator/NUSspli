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
#include <string.h>

#include <input.h>
#include <menu/utils.h>
#include <renderer.h>
#include <state.h>
#include <thread.h>
#include <ui.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#pragma GCC diagnostic pop

// Depth of the screen stack: the root screen plus the chain of modals the
// screens push from inside their update. The deepest chain possible today
// runs main menu -> missing content -> list -> predownload -> question ->
// error dialog, six of the eight slots; a push above the top is refused by
// uiPush() with a debug message instead of running off the array.
#define UI_MAX_SCREENS 8
#define UI_ERROR_MSG   512
// One frame of uiYield(): the caller sleeps instead of presenting, at the
// pace of the loop instead of a hardcoded 60 Hz slice.
#define UI_WAIT_TICKS   OSMillisecondsToTicks(1000 / FRAMERATE)
#define UI_FRAME_MS     (1000 / FRAMERATE)
#define UI_DELTA_MAX_MS (UI_FRAME_MS * 4)
// Speed of the screen transition in units per second: a quarter second
// from the picture of the screen that left to the one that arrived.
#define UI_FADE_RATE 4.0f

static const UIScreen *screenStack[UI_MAX_SCREENS];
static int stackTop = -1;
static int modalResult = 0;
static bool running = false;
static bool frameDirty = true;
// Set by uiPump: the buffer holds a frame the engine filled and owns it
// until the flow it belongs to is back in the frame that started it.
static bool engineFlow = false;

// The transition between two screens: how much of the new picture is up
// (0 still shows the picture of the screen that left, 1 the arrived one).
static UiAnim fade = { .value = 1.0f, .target = 1.0f };

// Removes the top screen without a transition: the callers below unwind
// a stack that has to be gone (an exiting app, the cleanup of a modal
// loop), where a blend would only start a picture nothing shows.
static void popNow()
{
    if(stackTop < 0)
        return;

    const UIScreen *screen = screenStack[stackTop];
    screenStack[stackTop] = NULL;
    --stackTop;

    if(screen->leave != NULL)
        screen->leave();

    // An engine flow owns the frame (uiPump presented it): the screen
    // below is stale and rebuilding it would flash up between two
    // engine frames. The frame the flow ends in marks the picture dirty
    // again, see uiFrame.
    frameDirty = !engineFlow;
}

// Starts the transition over the picture that is on screen: that picture
// is kept as the start of the blend and the new one takes over. Without
// one to keep (the first screen of the app) the new picture simply
// appears.
static void startTransition()
{
    // A chain of pops shares the blend it starts with: the finished
    // dialog, the queue screen and the menu behind them are gone within
    // a few frames, and a restart per step would show a fade for every
    // screen in between. The picture the running blend starts from is
    // still the one on screen, so it stays and the chain lands in the
    // screen it ends in.
    if(fade.value < fade.target)
        return;

    fade.value = takeSnapshot() ? 0.0f : 1.0f;
    uiAnimTo(&fade, 1.0f);
}

// One step of the screen transition: the kept picture lies over the new
// frame and gets lighter every step, so the one turns into the other
// without passing through black. fresh says whether the frame was
// rebuilt, because only then the blend lands on a cleared picture: a
// retained one would collect the old picture of every pass.
static void stepTransition(bool fresh)
{
    if(fade.target >= 1.0f && fade.value >= 1.0f)
        return;

    float shown = uiAnimStep(&fade, UI_FADE_RATE);

    if(fresh)
        blendSnapshot((uint8_t)((1.0f - shown) * 255.0f));

    // Keep the screen rebuilding until the transition is through.
    frameDirty = true;
}

// Animation clock of the loop, see uiFrameCount/uiDeltaMs
static uint32_t frameCount = 0;
static uint32_t deltaMs = UI_FRAME_MS;
static OSTime lastFrame = 0;

static spinlock eventLock = SPINLOCK_FREE;
static char pendingError[UI_ERROR_MSG];
static bool errorPending = false;

void uiDrainEvents()
{
    char msg[UI_ERROR_MSG];

    spinLock(eventLock);
    bool pending = errorPending;
    if(pending)
    {
        OSBlockMove(msg, pendingError, sizeof(msg), false);
        pendingError[0] = '\0';
        errorPending = false;
    }
    spinReleaseLock(eventLock);

    if(!pending)
        return;

    // The message is out of the slot now: a second poster can fill it
    // again while this dialog is up. Posted messages are unrecoverable by
    // contract (see uiPostError), so the app is asked to leave after the
    // user dismissed it - the very consequence the I/O queue used to take
    // itself, but here without asking which thread is standing in.
    showErrorFrame(msg);

    if(AppRunning(true))
        homeButtonCallback((void *)true);
}

// Animation clock: how far the frames actually are apart. Ticked by
// everything that presents (uiFrame and the blocking pumps below), so an
// animation keeps its speed during an engine flow too and a clock that
// jumps backwards (system time adjusted) counts as one normal frame
// instead of a negative step.
static void tickClock()
{
    OSTime now = OSGetTime();
    if(lastFrame == 0 || now <= lastFrame)
        deltaMs = UI_FRAME_MS;
    else
    {
        uint64_t ms = OSTicksToMilliseconds(now - lastFrame);
        deltaMs = ms > UI_DELTA_MAX_MS ? UI_DELTA_MAX_MS : (uint32_t)ms;
    }

    lastFrame = now;
    ++frameCount;
}

void uiRun()
{
    // Everything that pumps from now on is this loop: it runs on the main
    // thread and every worker reaches the UI through uiPostError instead
    // of standing in front of it.
    running = true;
    while(running)
    {
        // A flow may leave its frame behind when it ends outside the frame
        // that started it, so the marker never reaches the frame end that
        // normally clears it. Take it over here: no engine code is below
        // this loop, the screen below has to be rebuilt again.
        if(engineFlow)
        {
            engineFlow = false;
            frameDirty = true;
        }

        uiFrame();
    }
}

void uiExit()
{
    running = false;

    // Screens are torn down in reverse order so leave handlers (saving,
    // closing overlays) run, and the frame the exiting screen left behind
    // (the goodbye frame) stays what gets presented. popNow() instead of
    // uiPop(): a blend would start a picture that nothing presents any
    // more, and the goodbye frame has to stay on screen.
    while(stackTop >= 0)
        popNow();
}

void uiPush(const UIScreen *screen, void *param)
{
    if(screen == NULL)
        return;

    if(stackTop + 1 == UI_MAX_SCREENS)
    {
        debugPrintf("UI: screen stack overflow pushing \"%s\"", screen->name);
        return;
    }

    ++stackTop;
    screenStack[stackTop] = screen;
    if(screen->enter != NULL)
        screen->enter(param);

    startTransition();

    frameDirty = true;
}

void uiPop()
{
    if(stackTop == -1)
        return;

    // The picture of the screen that leaves is the start of the blend,
    // the screen itself is gone at once: the one below takes the input
    // right away and only its picture waits for the transition.
    startTransition();

    popNow();
}

bool uiTopIs(const UIScreen *screen)
{
    return stackTop != -1 && screenStack[stackTop] == screen;
}

void uiSetResult(int result)
{
    modalResult = result;
}

int uiModal(const UIScreen *screen, void *param)
{
    int base = stackTop;
    modalResult = 0;
    uiPush(screen, param);

    // Not AppRunning(true): uiFrame() pumps every iteration anyway and two
    // event pumps per frame are one too many. AppRunning(false) only observes
    // the state, which also keeps the dialogs that run outside uiRun() (the
    // update check during boot) working - "running" would be false there.
    while(stackTop > base && AppRunning(false))
        uiFrame();

    // The app stopped while the dialog was up: unwind instead of leaving
    // dangling screens below the caller, and without a transition (the
    // picture below is not going to be drawn again).
    while(stackTop > base)
        popNow();

    return modalResult;
}

void uiInvalidate()
{
    frameDirty = true;
}

uint32_t uiFrameCount()
{
    return frameCount;
}

uint32_t uiDeltaMs()
{
    return deltaMs;
}

void uiAnimTo(UiAnim *anim, float target)
{
    if(anim != NULL)
        anim->target = target;
}

float uiAnimStep(UiAnim *anim, float rate)
{
    if(anim == NULL)
        return 0.0f;

    // The distance of one frame at the given speed in units per second:
    // the motion only depends on the clock, not on FRAMERATE, and the clamped
    // delta keeps a stall from skipping over the goal.
    float step = rate * (float)deltaMs / 1000.0f;

    if(anim->value < anim->target)
    {
        anim->value += step;
        if(anim->value > anim->target)
            anim->value = anim->target;
    }
    else if(anim->value > anim->target)
    {
        anim->value -= step;
        if(anim->value < anim->target)
            anim->value = anim->target;
    }

    return anim->value;
}

void uiFrame()
{
    if(!AppRunning(true))
    {
        running = false;
        return;
    }

    // The two states every legacy menu loop checked, now in the one place
    // they belong: while the app sits in the background nothing is read or
    // drawn (the pump above keeps running), and the state after coming back
    // rebuilds the picture once - the retained frame may be gone by then.
    // state.h keeps both for the UI rewrites still to come.
    if(app == APP_STATE_BACKGROUND)
        return;
    if(app == APP_STATE_RETURNING)
        frameDirty = true;

    // Animation clock of every presented frame, see tickClock.
    tickClock();

    // Who may release the engine marker: a dialog opened from inside uiPump
    // runs this as a nested frame and has to leave it alone, while the frame
    // the flow started in owns it from here on.
    bool engineAtEntry = engineFlow;

    uiDrainEvents();
    readInput();

    if(stackTop != -1)
    {
        const UIScreen *screen = screenStack[stackTop];

        // A press of a button the screen declares rebuilds the picture -
        // this is where the old loops set redraw = true after every trigger
        // input. Content that changes without a trigger (the D-pad repeat
        // blocks of the list menus, the retry countdown) calls uiInvalidate()
        // where the old loops did it by hand. At rest the retained frame is
        // presented unchanged, which keeps text heavy screens on the
        // FRAMERATE pace.
        if(vpad.trigger && (screen->buttons == 0 || (vpad.trigger & screen->buttons)))
            frameDirty = true;

        // update is required (see UIScreen), render may be NULL for the
        // overlay dialogs that keep the picture below.
        screen->update();

        // The flow that started in this frame ended with the update, so the
        // buffer holds its last frame: rebuild the screen below instead of
        // presenting engine content to the menus.
        if(engineFlow && !engineAtEntry)
        {
            engineFlow = false;
            frameDirty = true;
        }

        // The update may have popped or pushed screens (or exited the app),
        // so gate on whatever is on top now.
        if(stackTop != -1)
        {
            screen = screenStack[stackTop];
            bool rebuilt = false;
            if(frameDirty && screen->render != NULL)
            {
                screen->render();
                rebuilt = true;
            }

            frameDirty = false;
            stepTransition(rebuilt);
        }
    }

    // The frame can outlive the app: the HOME confirmation runs as a dialog
    // inside the AppRunning() at the top of this frame (see
    // homeButtonCallback), and the screens below it unwind while this one is
    // still on its way to the present. What was rebuilt above is then the
    // screen under the one the user left, and presenting it would flash that
    // screen between the confirmation and the goodbye picture. Not
    // AppRunning(true): this frame pumped already, two event pumps per frame
    // are one too many (see uiModal).
    if(!AppRunning(false))
    {
        running = false;
        return;
    }

    drawFrame();
}

void uiWaitKey()
{
    while(AppRunning(true))
    {
        tickClock();
        uiDrainEvents();
        readInput();
        if(vpad.trigger)
            break;

        drawFrame();
    }

    uiInvalidate(); // whatever the caller does next sees a fresh picture
}

void uiWaitWhile(volatile bool *condition, UiWaitFrame frame, void *ctx)
{
    while(condition != NULL && *condition && AppRunning(true))
    {
        // Same ownership as uiPump: the frame the callback fills belongs to
        // the engine, a dialog that pops in between must not flash the
        // screen below. Without a callback the retained picture stays the
        // one the UI last drew, so the marker is left alone.
        if(frame != NULL)
            engineFlow = true;

        tickClock();
        uiDrainEvents();
        readInput();
        if(frame != NULL)
            frame(ctx);

        stepTransition(frame != NULL);

        drawFrame();
    }

    uiInvalidate(); // the engine content is gone, redraw the screen below
}

void uiPump(UiWaitFrame frame, void *ctx)
{
    // First, not last: a dialog can be pumped from inside uiDrainEvents or
    // readInput and has to see the marker when it pops.
    engineFlow = true;

    tickClock();
    uiDrainEvents();
    readInput();
    if(frame != NULL)
        frame(ctx);

    stepTransition(frame != NULL);

    drawFrame();
}

void uiPauseRenderer()
{
    pauseRenderer();
}

void uiResumeRenderer()
{
    resumeRenderer();
}

void uiDrawByeFrame()
{
    drawByeFrame();
}

void uiYield()
{
    // Only a sleep: the callers wait inside the I/O queue, where the error
    // itself (checkForQueueErrors) ends the wait, and the dialog of a
    // posted message belongs to a frame - see uiDrainEvents.
    OSSleepTicks(UI_WAIT_TICKS);
}

void *uiShowOverlay(const char *text)
{
    return addErrorOverlay(text);
}

void uiHideOverlay(void *overlay)
{
    removeErrorOverlay(overlay);
}

void uiPresentFrame()
{
    drawFrame();
}

void uiPostError(const char *text)
{
    size_t len = strlen(text);

    spinLock(eventLock);
    // One slot, first message wins: a second poster would have to overwrite
    // the one that is still waiting, which would drop a dialog nobody has
    // seen. There is no wait for the slot either - a loop thread that posts
    // here stands in its own frame, waits for itself and would deadlock, so
    // it leaves the message for uiDrainEvents to pick up like everybody
    // else.
    if(!errorPending)
    {
        if(len >= sizeof(pendingError))
            len = sizeof(pendingError) - 1;

        OSBlockMove(pendingError, text, len, false);
        pendingError[len] = '\0';
        errorPending = true;
    }
    spinReleaseLock(eventLock);
}
