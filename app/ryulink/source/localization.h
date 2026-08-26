#pragma once

#include <stdbool.h>

/**
 * Initializes the UI language from the Nintendo Switch system setting.
 * Simplified Chinese is selected for any Chinese system language; English is
 * the fallback for every other language until that language is translated.
 */
void ryuLinkLocalizationInitialize(void);
void ryuLinkLocalizationExit(void);
bool ryuLinkLocalizationUsesChinese(void);

/** Returns the translation matching the current system language. */
const char *ryuLinkLocalize(const char *english, const char *chinese);
