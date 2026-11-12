// ============================================================================
// auto_bind.h — One-button camera auto-binding
// ============================================================================
// Press the board button (AUTOBIND_BUTTON_PIN): one bounded BLE scan collects
// every advertiser whose name looks like a DJI / GoPro camera and saves the
// ones not yet in the registry.  While it runs the status LED blinks fast.
// ============================================================================

#ifndef AUTO_BIND_H
#define AUTO_BIND_H

#include <stdint.h>

enum AutoBindLed {
    AUTOBIND_LED_NONE = 0,   // Not active — normal status LED pattern
    AUTOBIND_LED_FAST,       // Scanning
    AUTOBIND_LED_OK,         // Finished, at least one camera added (solid)
    AUTOBIND_LED_NOTHING     // Finished, nothing new (off)
};

void autoBindInit();
void autoBindUpdate();   // Call from loop(); non-blocking

/// What the status LED should do right now.
AutoBindLed autoBindLedMode();

/// Start a bind run (same as pressing the button).  False if refused.
bool autoBindStart();

#endif // AUTO_BIND_H
