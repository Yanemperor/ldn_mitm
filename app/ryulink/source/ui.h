#pragma once

#include <switch.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    Framebuffer framebuffer;
    bool initialized;
} RyuLinkUi;

enum {
    RyuLinkScreenWidth = 1280,
    RyuLinkScreenHeight = 720,
};

bool ryuLinkUiInitialize(RyuLinkUi *ui);
void ryuLinkUiExit(RyuLinkUi *ui);
void ryuLinkUiBegin(RyuLinkUi *ui);
void ryuLinkUiEnd(RyuLinkUi *ui);

void ryuLinkUiFill(u32 color);
void ryuLinkUiRect(int x, int y, int width, int height, u32 color);
void ryuLinkUiText(int x, int y, int scale, u32 color, const char *text);
void ryuLinkUiCenteredText(int y, int scale, u32 color, const char *text);
/** Draw UTF-8 text in up to max_lines, wrapping before the supplied width. */
void ryuLinkUiWrappedText(int x, int y, int width, int scale, int line_height,
                          int max_lines, u32 color, const char *text);
void ryuLinkUiRoundedPanel(int x, int y, int width, int height, int radius, u32 color);
