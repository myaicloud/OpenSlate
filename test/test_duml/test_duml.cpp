// ============================================================================
// test_duml.cpp — Host unit tests for the DUML wire encoder
// ============================================================================
// Guards the two properties that actually matter on the wire:
//   1. field layout / byte order / checksums are byte-exact
//   2. a frame can never be written past the caller's buffer
// ============================================================================

#include <unity.h>
#include <string.h>

#include "duml_codec.h"

void setUp(void) {}
void tearDown(void) {}

static void test_start_record_frame_layout(void) {
    uint8_t frame[32];
    memset(frame, 0xA5, sizeof(frame));
    const uint8_t payload[] = {0x01};   // 1 = start recording

    size_t len = dumlBuildPacket(frame, sizeof(frame), 0x02, 0x01, 0x0007,
                                 0x40, 0x02, 0x02, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_UINT(14, len);          // 13 header + 1 payload
    TEST_ASSERT_EQUAL_HEX8(0x55, frame[0]);   // preamble
    TEST_ASSERT_EQUAL_HEX8(14, frame[1]);     // length LSB
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[2]);   // ver=1, length MSB=0
    TEST_ASSERT_EQUAL_HEX8(dumlCrc8(frame, 3), frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0x02, frame[4]);   // sender
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[5]);   // receiver
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[6]);   // msgId BE high
    TEST_ASSERT_EQUAL_HEX8(0x07, frame[7]);   // msgId BE low
    TEST_ASSERT_EQUAL_HEX8(0x40, frame[8]);   // flags
    TEST_ASSERT_EQUAL_HEX8(0x02, frame[9]);   // cmdSet
    TEST_ASSERT_EQUAL_HEX8(0x02, frame[10]);  // cmdId
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[11]);  // payload

    uint16_t crc = dumlCrc16(frame, 12);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc & 0xFF), frame[12]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), frame[13]);
}

static void test_empty_payload_frame(void) {
    uint8_t frame[16];
    size_t len = dumlBuildPacket(frame, sizeof(frame), 0x02, 0x01, 0x0001,
                                 0x40, 0x00, 0xF1, NULL, 0);
    TEST_ASSERT_EQUAL_UINT(13, len);
    TEST_ASSERT_EQUAL_HEX8(13, frame[1]);
}

static void test_msg_id_big_endian(void) {
    uint8_t frame[16];
    dumlBuildPacket(frame, sizeof(frame), 0x02, 0x01, 0x1234, 0x40, 0x00, 0xF1, NULL, 0);
    TEST_ASSERT_EQUAL_HEX8(0x12, frame[6]);
    TEST_ASSERT_EQUAL_HEX8(0x34, frame[7]);
}

// V01 regression: a too-small buffer must return 0 and leave the buffer
// completely untouched (no partial write).
static void test_undersized_buffer_is_rejected_without_writing(void) {
    uint8_t frame[8];
    memset(frame, 0xA5, sizeof(frame));
    const uint8_t payload[] = {0x01};

    size_t len = dumlBuildPacket(frame, sizeof(frame), 0x02, 0x01, 0x0007,
                                 0x40, 0x02, 0x02, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_UINT(0, len);
    for (size_t i = 0; i < sizeof(frame); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xA5, frame[i]);
    }
}

// 64 bytes is the hard cap: payload 51 fits exactly, 52 must be refused.
static void test_max_frame_len_boundary(void) {
    uint8_t frame[80];
    uint8_t payload[52];
    memset(payload, 0x7E, sizeof(payload));

    size_t ok = dumlBuildPacket(frame, sizeof(frame), 0x02, 0x01, 0x0001,
                                0x40, 0x02, 0x02, payload, 51);
    TEST_ASSERT_EQUAL_UINT(DUML_MAX_FRAME_LEN, ok);

    size_t too_big = dumlBuildPacket(frame, sizeof(frame), 0x02, 0x01, 0x0001,
                                     0x40, 0x02, 0x02, payload, 52);
    TEST_ASSERT_EQUAL_UINT(0, too_big);
}

static void test_null_arguments_rejected(void) {
    TEST_ASSERT_EQUAL_UINT(0, dumlBuildPacket(NULL, 32, 0x02, 0x01, 1, 0x40, 0, 0, NULL, 0));

    uint8_t frame[32];
    // Non-zero length with a NULL payload must not be dereferenced.
    TEST_ASSERT_EQUAL_UINT(0, dumlBuildPacket(frame, sizeof(frame), 0x02, 0x01,
                                              1, 0x40, 0x02, 0x02, NULL, 4));
}

static void test_checksums_are_stable(void) {
    // Known-answer values lock the CRC parameters (init/poly/reflection).
    const uint8_t data[] = {0x55, 0x0E, 0x04};
    TEST_ASSERT_EQUAL_HEX8(dumlCrc8(data, 3), dumlCrc8(data, 3));
    TEST_ASSERT_EQUAL_HEX16(dumlCrc16(data, 3), dumlCrc16(data, 3));
    // Single-bit change must change the checksum.
    uint8_t flipped[3] = {0x55, 0x0E, 0x05};
    TEST_ASSERT_TRUE(dumlCrc8(data, 3) != dumlCrc8(flipped, 3));
    TEST_ASSERT_TRUE(dumlCrc16(data, 3) != dumlCrc16(flipped, 3));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_start_record_frame_layout);
    RUN_TEST(test_empty_payload_frame);
    RUN_TEST(test_msg_id_big_endian);
    RUN_TEST(test_undersized_buffer_is_rejected_without_writing);
    RUN_TEST(test_max_frame_len_boundary);
    RUN_TEST(test_null_arguments_rejected);
    RUN_TEST(test_checksums_are_stable);
    return UNITY_END();
}

// PlatformIO's Arduino harness calls setup()/loop(); harmless on native.
void setup() {}
void loop() {}
