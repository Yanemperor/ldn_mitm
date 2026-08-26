#include "ui.h"
#include "localization.h"

#include <ctype.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

static u32 *g_pixels;
static u32 g_stride;
static FT_Library g_font_library;
static FT_Face g_chinese_face;
static int g_chinese_scale;
static bool g_pl_initialized;

/* Compact 5x7 display face. Each glyph stores one row per byte, left aligned. */
static const uint8_t g_glyphs[][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
    {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F}, /* C */
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
    {0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}, /* 5 */
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}, /* 9 */
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* - */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}, /* _ */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04}, /* . */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x04}, /* , */
    {0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00}, /* : */
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}, /* / */
    {0x0E, 0x02, 0x02, 0x04, 0x08, 0x00, 0x04}, /* ? */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, /* ! */
    {0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' */
    {0x06, 0x08, 0x08, 0x08, 0x08, 0x08, 0x06}, /* ( */
    {0x0C, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0C}, /* ) */
};

static const uint8_t *glyph_for(char character) {
    unsigned char value = (unsigned char)toupper((unsigned char)character);
    if (value >= 'A' && value <= 'Z') return g_glyphs[value - 'A'];
    if (value >= '0' && value <= '9') return g_glyphs[26 + value - '0'];
    if (value == '-') return g_glyphs[36];
    if (value == '_') return g_glyphs[37];
    if (value == '.') return g_glyphs[38];
    if (value == ',') return g_glyphs[39];
    if (value == ':') return g_glyphs[40];
    if (value == '/') return g_glyphs[41];
    if (value == '?') return g_glyphs[42];
    if (value == '!') return g_glyphs[43];
    if (value == '\'') return g_glyphs[44];
    if (value == '(') return g_glyphs[45];
    if (value == ')') return g_glyphs[46];
    return NULL;
}

static void draw_chinese_glyph(int x, int y, int scale, u32 color, uint32_t codepoint) {
    FT_GlyphSlot slot;
    FT_UInt glyph;

    if (!g_chinese_face) return;
    if (g_chinese_scale != scale) {
        if (FT_Set_Pixel_Sizes(g_chinese_face, 0, (FT_UInt)(scale * 7))) return;
        g_chinese_scale = scale;
    }
    glyph = FT_Get_Char_Index(g_chinese_face, codepoint);
    if (!glyph || FT_Load_Glyph(g_chinese_face, glyph, FT_LOAD_DEFAULT) ||
        FT_Render_Glyph(g_chinese_face->glyph, FT_RENDER_MODE_NORMAL)) {
        return;
    }

    slot = g_chinese_face->glyph;
    for (unsigned int row = 0; row < slot->bitmap.rows; ++row) {
        int target_y = y + (scale * 7 - slot->bitmap_top) + (int)row;
        if (target_y < 0 || target_y >= RyuLinkScreenHeight) continue;
        for (unsigned int column = 0; column < slot->bitmap.width; ++column) {
            int target_x = x + slot->bitmap_left + (int)column;
            if (target_x < 0 || target_x >= RyuLinkScreenWidth) continue;
            if (slot->bitmap.buffer[row * slot->bitmap.pitch + column] >= 96) {
                g_pixels[target_y * g_stride + target_x] = color;
            }
        }
    }
}

static int chinese_advance(int scale, uint32_t codepoint) {
    if (!g_chinese_face) return scale * 7;
    if (g_chinese_scale != scale) {
        if (FT_Set_Pixel_Sizes(g_chinese_face, 0, (FT_UInt)(scale * 7))) return scale * 7;
        g_chinese_scale = scale;
    }
    if (FT_Load_Char(g_chinese_face, codepoint, FT_LOAD_DEFAULT)) return scale * 7;
    return (int)(g_chinese_face->glyph->advance.x >> 6);
}

static int text_width(int scale, const char *text) {
    int width = 0;
    for (size_t index = 0; text[index];) {
        uint32_t codepoint;
        ssize_t bytes = decode_utf8(&codepoint, (const uint8_t *)&text[index]);
        if (bytes <= 0) break;
        index += (size_t)bytes;
        width += codepoint < 0x80 ? scale * 6 : chinese_advance(scale, codepoint);
    }
    return width > 0 ? width - scale : 0;
}

bool ryuLinkUiInitialize(RyuLinkUi *ui) {
    memset(ui, 0, sizeof(*ui));
    if (R_FAILED(viInitialize(ViServiceType_Application))) return false;
    if (R_FAILED(framebufferCreate(&ui->framebuffer, nwindowGetDefault(),
                                   RyuLinkScreenWidth, RyuLinkScreenHeight,
                                   PIXEL_FORMAT_RGBA_8888, 2)) ||
        R_FAILED(framebufferMakeLinear(&ui->framebuffer))) {
        framebufferClose(&ui->framebuffer);
        viExit();
        return false;
    }
    if (ryuLinkLocalizationUsesChinese() && R_SUCCEEDED(plInitialize(PlServiceType_User))) {
        PlFontData font;
        g_pl_initialized = true;
        if (R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_ChineseSimplified)) &&
            !FT_Init_FreeType(&g_font_library) &&
            FT_New_Memory_Face(g_font_library, font.address, font.size, 0, &g_chinese_face)) {
            FT_Done_FreeType(g_font_library);
            g_font_library = NULL;
        }
    }
    ui->initialized = true;
    return true;
}

void ryuLinkUiExit(RyuLinkUi *ui) {
    if (!ui->initialized) return;
    if (g_chinese_face) FT_Done_Face(g_chinese_face);
    if (g_font_library) FT_Done_FreeType(g_font_library);
    g_chinese_face = NULL;
    g_font_library = NULL;
    g_chinese_scale = 0;
    if (g_pl_initialized) plExit();
    g_pl_initialized = false;
    framebufferClose(&ui->framebuffer);
    viExit();
    ui->initialized = false;
}

void ryuLinkUiBegin(RyuLinkUi *ui) {
    void *buffer = framebufferBegin(&ui->framebuffer, &g_stride);
    g_pixels = buffer;
    g_stride /= sizeof(*g_pixels);
}

void ryuLinkUiEnd(RyuLinkUi *ui) {
    framebufferEnd(&ui->framebuffer);
    g_pixels = NULL;
}

void ryuLinkUiFill(u32 color) {
    for (int y = 0; y < RyuLinkScreenHeight; ++y) {
        for (int x = 0; x < RyuLinkScreenWidth; ++x) g_pixels[y * g_stride + x] = color;
    }
}

void ryuLinkUiRect(int x, int y, int width, int height, u32 color) {
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    int right = x + width > RyuLinkScreenWidth ? RyuLinkScreenWidth : x + width;
    int bottom = y + height > RyuLinkScreenHeight ? RyuLinkScreenHeight : y + height;
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) g_pixels[row * g_stride + column] = color;
    }
}

void ryuLinkUiRoundedPanel(int x, int y, int width, int height, int radius, u32 color) {
    ryuLinkUiRect(x + radius, y, width - radius * 2, height, color);
    ryuLinkUiRect(x, y + radius, width, height - radius * 2, color);
    for (int offset = 0; offset < radius; ++offset) {
        int inset = radius - offset - 1;
        ryuLinkUiRect(x + inset, y + offset, width - inset * 2, 1, color);
        ryuLinkUiRect(x + inset, y + height - offset - 1, width - inset * 2, 1, color);
    }
}

void ryuLinkUiText(int x, int y, int scale, u32 color, const char *text) {
    for (size_t index = 0; text[index];) {
        uint32_t codepoint;
        ssize_t bytes = decode_utf8(&codepoint, (const uint8_t *)&text[index]);
        if (bytes <= 0) break;
        index += (size_t)bytes;
        if (codepoint >= 0x80) {
            draw_chinese_glyph(x, y, scale, color, codepoint);
            x += chinese_advance(scale, codepoint);
            continue;
        }
        const uint8_t *glyph = glyph_for((char)codepoint);
        if (!glyph) {
            x += scale * 6;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if (glyph[row] & (1U << (4 - column))) {
                    ryuLinkUiRect(x + column * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += scale * 6;
    }
}

void ryuLinkUiCenteredText(int y, int scale, u32 color, const char *text) {
    int width = text_width(scale, text);
    ryuLinkUiText((RyuLinkScreenWidth - width) / 2, y, scale, color, text);
}

void ryuLinkUiWrappedText(int x, int y, int width, int scale, int line_height,
                          int max_lines, u32 color, const char *text) {
    char line[385];
    size_t line_length = 0;
    int lines = 0;

    if (!text || width <= 0 || max_lines <= 0) return;
    while (*text && lines < max_lines) {
        size_t character_length = 1;
        char character[5] = {0};

        if (((unsigned char)*text & 0x80) != 0) {
            while (character_length < 4 && ((unsigned char)text[character_length] & 0xc0) == 0x80) {
                ++character_length;
            }
        }
        if (line_length + character_length >= sizeof(line)) break;
        memcpy(character, text, character_length);
        memcpy(line + line_length, character, character_length);
        line_length += character_length;
        line[line_length] = '\0';

        if (*text == '\n' || text_width(scale, line) > width) {
            if (*text != '\n') {
                line_length -= character_length;
                line[line_length] = '\0';
            }
            if (line_length) {
                ryuLinkUiText(x, y + lines * line_height, scale, color, line);
                ++lines;
            }
            line_length = 0;
            line[0] = '\0';
            if (*text != '\n' && lines < max_lines) continue;
        }
        text += character_length;
    }
    if (line_length && lines < max_lines) {
        ryuLinkUiText(x, y + lines * line_height, scale, color, line);
    }
}
