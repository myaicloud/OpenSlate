// ============================================================================
// autobind_match.cpp — Name-based camera classification
// ============================================================================

#include "autobind_match.h"
#include <stddef.h>

static char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/// Case-insensitive substring search; `needle` must already be lowercase.
static bool containsNoCase(const char *hay, const char *needle) {
    for (; *hay; hay++) {
        size_t i = 0;
        while (needle[i] && hay[i] && lowerAscii(hay[i]) == needle[i]) i++;
        if (!needle[i]) return true;
    }
    return false;
}

AutoBindBrand autobindClassifyName(const char *name) {
    if (!name || !*name) return AUTOBIND_NONE;

    if (containsNoCase(name, "gopro") || containsNoCase(name, "hero") ||
        (lowerAscii(name[0]) == 'g' && lowerAscii(name[1]) == 'p')) {
        return AUTOBIND_GOPRO;
    }
    if (containsNoCase(name, "dji") || containsNoCase(name, "osmo") ||
        containsNoCase(name, "action")) {
        return AUTOBIND_DJI;
    }
    return AUTOBIND_NONE;
}
