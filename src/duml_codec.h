// ============================================================================
// duml_codec.h — DJI DUML packet encoding (pure, host-testable)
// ============================================================================
// Extracted from dji_camera.cpp so the wire format can be unit-tested on the
// host without pulling in NimBLE/Arduino. No RTOS, no BLE, no allocation.
// ============================================================================

#ifndef DUML_CODEC_H
#define DUML_CODEC_H

#include <stddef.h>
#include <stdint.h>

// Largest DUML frame this firmware ever builds (13-byte header + payload).
#define DUML_MAX_FRAME_LEN 64

/// CRC-8 with reflected init 0x77 / reflected poly 0x8C (DUML header).
uint8_t dumlCrc8(const uint8_t *data, size_t len);

/// CRC-16 with reflected init 0x3692 / reflected poly 0x8408 (DUML trailer).
uint16_t dumlCrc16(const uint8_t *data, size_t len);

/// Build a complete DUML frame (header + target + msgId + type + payload +
/// CRC16) into `buffer`.
/// @return Bytes written, or 0 when the frame would not fit `bufCap` or
///         would exceed DUML_MAX_FRAME_LEN. Never writes past `bufCap`.
size_t dumlBuildPacket(uint8_t *buffer, size_t bufCap,
                       uint8_t sender, uint8_t receiver, uint16_t msgId,
                       uint8_t flags, uint8_t cmdSet, uint8_t cmdId,
                       const uint8_t *payload, size_t payloadLen);

#endif // DUML_CODEC_H
