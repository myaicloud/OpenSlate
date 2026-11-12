// ============================================================================
// test/stub/Arduino.h — Minimal host-side stand-in for the Arduino core
// ============================================================================
// Only what the host-testable modules (msp_protocol, duml_codec) need.
// Kept deliberately tiny so a missing symbol fails loudly instead of being
// silently faked.
// ============================================================================

#ifndef ARDUINO_STUB_H
#define ARDUINO_STUB_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SERIAL_8N1 0x800001c

class HardwareSerial {
public:
    void begin(unsigned long, uint32_t = SERIAL_8N1, int8_t = -1, int8_t = -1) {}
    size_t write(const uint8_t *, size_t) { return 0; }
    int available() { return 0; }
    int read() { return -1; }
};

inline unsigned long& stubMillis() {
    static unsigned long now = 0;
    return now;
}
inline unsigned long millis() { return stubMillis(); }
inline void delay(unsigned long) {}

inline size_t strlcpy(char *dest, const char *src, size_t size) {
    size_t length = strlen(src);
    if (size) {
        size_t copied = length < size - 1 ? length : size - 1;
        memcpy(dest, src, copied);
        dest[copied] = '\0';
    }
    return length;
}

#endif // ARDUINO_STUB_H
