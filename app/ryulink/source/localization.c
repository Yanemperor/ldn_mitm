#include "localization.h"

#include <switch.h>

static bool g_uses_chinese;
static bool g_set_initialized;

void ryuLinkLocalizationInitialize(void) {
    u64 language_code = 0;
    SetLanguage language;

    g_uses_chinese = false;
    if (R_FAILED(setInitialize())) return;
    g_set_initialized = true;
    if (R_FAILED(setGetSystemLanguage(&language_code)) ||
        R_FAILED(setMakeLanguage(language_code, &language))) {
        return;
    }

    g_uses_chinese = language == SetLanguage_ZHCN || language == SetLanguage_ZHHANS ||
                     language == SetLanguage_ZHTW || language == SetLanguage_ZHHANT;
}

void ryuLinkLocalizationExit(void) {
    if (!g_set_initialized) return;
    setExit();
    g_set_initialized = false;
}

bool ryuLinkLocalizationUsesChinese(void) {
    return g_uses_chinese;
}

const char *ryuLinkLocalize(const char *english, const char *chinese) {
    return g_uses_chinese ? chinese : english;
}
