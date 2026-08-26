#include "evidence_redaction.h"

#include <ctype.h>
#include <string.h>

enum { RedactionLineLimit = 2048 };

static bool append(char *output, size_t output_size, size_t *length,
                   const char *text, size_t text_length) {
    if (text_length >= output_size - *length) return false;
    memcpy(output + *length, text, text_length);
    *length += text_length;
    output[*length] = '\0';
    return true;
}

static bool secret_line(const char *line) {
    static const char *const Keys[] = {
        "authorization", "password", "passphrase", "access_token",
        "refresh_token", "id_token", "ticket", "client_secret",
    };
    char lower[RedactionLineLimit];
    size_t length = strlen(line);
    if (length >= sizeof(lower)) length = sizeof(lower) - 1;
    for (size_t index = 0; index < length; ++index) {
        lower[index] = (char)tolower((unsigned char)line[index]);
    }
    lower[length] = '\0';
    for (size_t index = 0; index < sizeof(Keys) / sizeof(Keys[0]); ++index) {
        if (strstr(lower, Keys[index]) != NULL) return true;
    }
    return false;
}

static bool ipv4_length(const char *text, size_t *out_length) {
    const char *cursor = text;
    if (text == NULL || !isdigit((unsigned char)*text)) return false;
    for (int part = 0; part < 4; ++part) {
        unsigned value = 0;
        int digits = 0;
        while (digits < 3 && isdigit((unsigned char)*cursor)) {
            value = value * 10U + (unsigned)(*cursor++ - '0');
            ++digits;
        }
        if (digits == 0 || value > 255U || isdigit((unsigned char)*cursor)) return false;
        if (part < 3 && *cursor++ != '.') return false;
    }
    *out_length = (size_t)(cursor - text);
    return true;
}

static bool mac_length(const char *text, size_t *out_length) {
    char separator;
    if (text == NULL || !isxdigit((unsigned char)text[0]) ||
        !isxdigit((unsigned char)text[1])) return false;
    separator = text[2];
    if (separator != ':' && separator != '-') return false;
    for (int part = 0; part < 6; ++part) {
        size_t offset = (size_t)part * 3;
        if (!isxdigit((unsigned char)text[offset]) ||
            !isxdigit((unsigned char)text[offset + 1])) return false;
        if (part < 5 && text[offset + 2] != separator) return false;
    }
    *out_length = 17;
    return true;
}

static size_t hex_run_length(const char *text) {
    size_t length = 0;
    while (isxdigit((unsigned char)text[length])) ++length;
    return length;
}

/**
 * Returns the byte length of one well-formed UTF-8 scalar, or zero for an
 * invalid/truncated byte sequence.  Evidence logs are supplied by third-party
 * sysmodules and crash reporters, so their bytes cannot be assumed to be text.
 */
static size_t utf8_sequence_length(const unsigned char *text) {
    const unsigned char first = text[0];
    if (first < 0x80) return 1;
    if (first >= 0xC2 && first <= 0xDF && text[1] != '\0' &&
        (text[1] & 0xC0) == 0x80) return 2;
    if (first == 0xE0 && text[1] >= 0xA0 && text[1] <= 0xBF &&
        text[2] != '\0' && (text[2] & 0xC0) == 0x80) return 3;
    if (((first >= 0xE1 && first <= 0xEC) || (first >= 0xEE && first <= 0xEF)) &&
        text[1] != '\0' && (text[1] & 0xC0) == 0x80 && text[2] != '\0' &&
        (text[2] & 0xC0) == 0x80) return 3;
    if (first == 0xED && text[1] >= 0x80 && text[1] <= 0x9F &&
        text[2] != '\0' && (text[2] & 0xC0) == 0x80) return 3;
    if (first == 0xF0 && text[1] >= 0x90 && text[1] <= 0xBF &&
        text[2] != '\0' && (text[2] & 0xC0) == 0x80 && text[3] != '\0' &&
        (text[3] & 0xC0) == 0x80) return 4;
    if (first >= 0xF1 && first <= 0xF3 && text[1] != '\0' &&
        (text[1] & 0xC0) == 0x80 && text[2] != '\0' &&
        (text[2] & 0xC0) == 0x80 && text[3] != '\0' &&
        (text[3] & 0xC0) == 0x80) return 4;
    if (first == 0xF4 && text[1] >= 0x80 && text[1] <= 0x8F &&
        text[2] != '\0' && (text[2] & 0xC0) == 0x80 && text[3] != '\0' &&
        (text[3] & 0xC0) == 0x80) return 4;
    return 0;
}

bool ryuLinkEvidenceRedactLine(const char *input, char *output,
                               size_t output_size, size_t *output_length) {
    const char *cursor = input;
    size_t written = 0;
    if (input == NULL || output == NULL || output_size == 0) return false;
    output[0] = '\0';
    if (secret_line(input)) {
        static const char Replacement[] = "<redacted-secret-line>\n";
        if (!append(output, output_size, &written, Replacement,
                    sizeof(Replacement) - 1)) return false;
        if (output_length != NULL) *output_length = written;
        return true;
    }
    while (*cursor) {
        size_t length = 0;
        const char *replacement = NULL;
        if (mac_length(cursor, &length)) replacement = "<mac>";
        else if ((cursor == input ||
                  (!isdigit((unsigned char)cursor[-1]) && cursor[-1] != '.')) &&
                 ipv4_length(cursor, &length)) replacement = "<ip>";
        else {
            length = hex_run_length(cursor);
            bool prefixed_address = cursor - input >= 2 && cursor[-2] == '0' &&
                    (cursor[-1] == 'x' || cursor[-1] == 'X');
            if (length >= 16 && !prefixed_address) replacement = "<id>";
        }
        if (replacement != NULL) {
            if (!append(output, output_size, &written, replacement,
                        strlen(replacement))) return false;
            cursor += length;
            continue;
        }
        size_t utf8_length = utf8_sequence_length((const unsigned char *)cursor);
        if (utf8_length > 1) {
            if (!append(output, output_size, &written, cursor, utf8_length)) return false;
            cursor += utf8_length;
            continue;
        }
        char character = *cursor++;
        if ((unsigned char)character >= 0x80) character = '?';
        if ((unsigned char)character < 0x20 && character != '\n' &&
            character != '\r' && character != '\t') character = ' ';
        if (!append(output, output_size, &written, &character, 1)) return false;
    }
    if (output_length != NULL) *output_length = written;
    return true;
}
