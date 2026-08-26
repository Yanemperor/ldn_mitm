/**
 * @file main.c
 * @brief RyuLink Task 1 application shell.
 */

#include <switch.h>

#include "app.h"
#include "ldn_mitm_ipc.h"
#include "localization.h"

static u64 stick_direction(HidAnalogStickState stick, bool *engaged) {
    const int threshold = 12000;
    int abs_x = stick.x < 0 ? -stick.x : stick.x;
    int abs_y = stick.y < 0 ? -stick.y : stick.y;

    if (abs_x < threshold && abs_y < threshold) {
        *engaged = false;
        return 0;
    }
    if (*engaged) return 0;
    *engaged = true;
    if (abs_x > abs_y) return stick.x < 0 ? HidNpadButton_Left : HidNpadButton_Right;
    return stick.y < 0 ? HidNpadButton_Down : HidNpadButton_Up;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    ryuLinkLocalizationInitialize();
    RyuLinkUi ui;
    if (!ryuLinkUiInitialize(&ui)) {
        ryuLinkLocalizationExit();
        return 1;
    }
    if (!ryuLinkAuthInitialize()) {
        ryuLinkUiExit(&ui);
        ryuLinkLocalizationExit();
        return 1;
    }
    /* Joining checks this service again. A missing sysmodule must not prevent
     * the player from signing in or seeing the remediation message. */
    (void)ryuLinkLdnMitmIpcInitialize();

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    RyuLinkApp app;
    ryuLinkAppInitialize(&app);
    bool stick_engaged = false;
    bool touch_was_down = false;
    static HidTouchScreenState touch_state;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 buttons = padGetButtonsDown(&pad);
        buttons |= stick_direction(padGetStickPos(&pad, 0), &stick_engaged);
        if (buttons & HidNpadButton_Plus) break;

        /* Execute the network action deferred by the previous input frame.
         * The loading overlay is already presented, so it stays visible
         * for the full (bounded) duration of the request. */
        ryuLinkAppRunPending(&app);

        ryuLinkAppUpdate(&app);
        ryuLinkAppHandleInput(&app, buttons);
        touch_state = (HidTouchScreenState){0};
        bool touch_is_down = hidGetTouchScreenStates(&touch_state, 1) > 0;
        if (touch_is_down && !touch_was_down) {
            ryuLinkAppHandleTouch(&app, touch_state.touches[0].x, touch_state.touches[0].y);
        }
        touch_was_down = touch_is_down;
        ryuLinkUiBegin(&ui);
        ryuLinkAppDraw(&app);
        ryuLinkUiEnd(&ui);
    }

    ryuLinkLdnMitmIpcExit();
    ryuLinkAuthExit();
    ryuLinkUiExit(&ui);
    ryuLinkLocalizationExit();
    return 0;
}
