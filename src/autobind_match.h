// ============================================================================
// autobind_match.h — Name-based camera classification (pure, host-testable)
// ============================================================================
// Decides from a BLE device name whether an advertiser looks like a DJI or a
// GoPro action camera.  No Arduino, no NimBLE, no allocation.
// ============================================================================

#ifndef AUTOBIND_MATCH_H
#define AUTOBIND_MATCH_H

/// Result of classifying an advertised device name.
enum AutoBindBrand {
    AUTOBIND_NONE  = 0,
    AUTOBIND_DJI   = 1,
    AUTOBIND_GOPRO = 2
};

/// Case-insensitive keyword match on the device name.
///   GoPro: contains "gopro" or "hero", or starts with "GP"
///   DJI  : contains "dji", "osmo" or "action"
/// NULL / empty names never match.
AutoBindBrand autobindClassifyName(const char *name);

#endif // AUTOBIND_MATCH_H
