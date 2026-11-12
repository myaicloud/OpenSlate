// ============================================================================
// duml_codec.cpp — DJI DUML packet encoding
// ============================================================================

#include "duml_codec.h"

uint8_t dumlCrc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x77;   // reflected init
    uint8_t poly = 0x8C;  // reflected poly
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x01) crc = (crc >> 1) ^ poly;
            else crc >>= 1;
        }
    }
    return crc;
}

uint16_t dumlCrc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0x3692;    // reflected init
    uint16_t poly = 0x8408;   // reflected poly
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ poly;
            else crc >>= 1;
        }
    }
    return crc;
}

size_t dumlBuildPacket(uint8_t *buffer, size_t bufCap,
                       uint8_t sender, uint8_t receiver, uint16_t msgId,
                       uint8_t flags, uint8_t cmdSet, uint8_t cmdId,
                       const uint8_t *payload, size_t payloadLen) {
    // V01: reject anything that would overflow the caller's buffer.
    size_t totalLen = 13 + payloadLen;
    if (totalLen > bufCap || totalLen > DUML_MAX_FRAME_LEN) return 0;
    if (!buffer) return 0;
    if (payloadLen && !payload) return 0;

    size_t idx = 0;

    buffer[idx++] = 0x55;
    buffer[idx++] = (uint8_t)(totalLen & 0xFF);
    buffer[idx++] = (uint8_t)(0x04 | ((totalLen >> 8) & 0x03)); // ver=1
    buffer[idx++] = dumlCrc8(buffer, 3);

    // Target
    buffer[idx++] = sender;
    buffer[idx++] = receiver;

    // Msg ID (BE)
    buffer[idx++] = (uint8_t)((msgId >> 8) & 0xFF);
    buffer[idx++] = (uint8_t)(msgId & 0xFF);

    // Type
    buffer[idx++] = flags;
    buffer[idx++] = cmdSet;
    buffer[idx++] = cmdId;

    // Payload
    for (size_t i = 0; i < payloadLen; i++) {
        buffer[idx++] = payload[i];
    }

    // CRC16 (LE)
    uint16_t crc = dumlCrc16(buffer, idx);
    buffer[idx++] = (uint8_t)(crc & 0xFF);
    buffer[idx++] = (uint8_t)((crc >> 8) & 0xFF);

    return idx;
}
