// ============================================================================
// test_msp.cpp — Host unit tests for the MSP v1 parser
// ============================================================================
// Focus: the parser is fed untrusted bytes from a UART. It must never read or
// write out of bounds, must reject bad checksums, and must resynchronise after
// garbage instead of wedging.
// ============================================================================

#include <unity.h>
#include <string.h>

#include "msp_protocol.h"

void setUp(void) {
    // Parser state is file-static; a fresh reset is forced by draining a
    // throwaway message through the state machine.
    MspMessage discard;
    mspParseByte('$', discard);
    mspParseByte('M', discard);
    mspParseByte('>', discard);
    mspParseByte(0, discard);
    mspParseByte(0, discard);
    mspParseByte(0, discard);
}

void tearDown(void) {}

// Feed a complete MSP v1 response frame; returns true when delivered.
static bool feedFrame(uint8_t cmd, const uint8_t *payload, uint8_t size, bool corruptCrc = false) {
    MspMessage msg;
    memset(&msg, 0, sizeof(msg));

    uint8_t crc = (uint8_t)(size ^ cmd);
    mspParseByte('$', msg);
    mspParseByte('M', msg);
    mspParseByte('>', msg);
    mspParseByte(size, msg);
    mspParseByte(cmd, msg);
    for (uint8_t i = 0; i < size; i++) {
        mspParseByte(payload[i], msg);
        crc ^= payload[i];
    }
    if (corruptCrc) crc ^= 0xFF;
    return mspParseByte(crc, msg);
}

static void test_parses_status_response(void) {
    // MSP_STATUS: cycle(2) i2c(2) sensors(2) flags(4) profile(1) = 11 bytes
    const uint8_t payload[11] = {0xE8, 0x03, 0, 0, 0x1F, 0,
                                 0x01, 0x00, 0x00, 0x00, 1};
    MspMessage out;
    memset(&out, 0, sizeof(out));

    uint8_t crc = 11 ^ MSP_STATUS;
    mspParseByte('$', out);
    mspParseByte('M', out);
    mspParseByte('>', out);
    mspParseByte(11, out);
    mspParseByte(MSP_STATUS, out);
    for (uint8_t i = 0; i < 11; i++) {
        mspParseByte(payload[i], out);
        crc ^= payload[i];
    }
    TEST_ASSERT_TRUE(mspParseByte(crc, out));

    TEST_ASSERT_EQUAL_UINT8(MSP_STATUS, out.cmd);
    TEST_ASSERT_EQUAL_UINT8(11, out.payloadSize);
    TEST_ASSERT_TRUE(out.valid);
    TEST_ASSERT_FALSE(out.isError);
    TEST_ASSERT_EQUAL_UINT16(0x03E8, mspReadU16(out, 0));      // cycleTime
    TEST_ASSERT_EQUAL_UINT32(0x00000001, mspReadU32(out, 6));  // flags
}

static void test_rejects_bad_crc(void) {
    const uint8_t payload[3] = {0x01, 0x02, 0x03};
    TEST_ASSERT_FALSE(feedFrame(MSP_ANALOG, payload, 3, true));
}

static void test_resyncs_after_garbage(void) {
    MspMessage out;
    memset(&out, 0, sizeof(out));
    const char *noise = "garbage$X$$random";
    for (size_t i = 0; noise[i]; i++) {
        mspParseByte((uint8_t)noise[i], out);
    }
    // A well-formed frame after the noise must still be delivered.
    const uint8_t payload[3] = {0x0A, 0x0B, 0x0C};
    TEST_ASSERT_TRUE(feedFrame(MSP_FC_VERSION, payload, 3));
}

// A declared payload larger than MSP_MAX_PAYLOAD_SIZE must be dropped, not
// buffered into the fixed array.
static void test_oversized_payload_dropped(void) {
    MspMessage out;
    memset(&out, 0, sizeof(out));
    const uint8_t big = MSP_MAX_PAYLOAD_SIZE + 1;

    mspParseByte('$', out);
    mspParseByte('M', out);
    mspParseByte('>', out);
    TEST_ASSERT_FALSE(mspParseByte(big, out));
    // Parser must be back at IDLE: a valid frame right after still parses.
    const uint8_t payload[2] = {0xDE, 0xAD};
    TEST_ASSERT_TRUE(feedFrame(MSP_API_VERSION, payload, 2));
}

// Truncated frame: the parser must not emit a bogus message, and it must not
// hang. A mid-frame resync is NOT possible for MSP v1 — 0x24 ('$') can occur
// legitimately inside binary payload, so the parser cannot distinguish a lost
// byte from data. The contract is therefore: the frame following a truncation
// may be swallowed, but delivery resumes on the one after that.
static void test_recovers_after_truncated_frame(void) {
    MspMessage out;
    memset(&out, 0, sizeof(out));

    mspParseByte('$', out);
    mspParseByte('M', out);
    mspParseByte('>', out);
    mspParseByte(4, out);      // claims 4 payload bytes
    mspParseByte(MSP_RC, out);
    mspParseByte(0x01, out);   // only 1 arrives, then the stream restarts

    // This frame is consumed as the leftover payload + a failed CRC check.
    const uint8_t lost[2] = {0x11, 0x22};
    feedFrame(MSP_FC_VARIANT, lost, 2);

    // The parser must be back in sync now.
    const uint8_t good[2] = {0x33, 0x44};
    TEST_ASSERT_TRUE(feedFrame(MSP_FC_VARIANT, good, 2));
}

// A truncated frame must never produce a delivered message on its own.
static void test_truncated_frame_emits_nothing(void) {
    MspMessage out;
    memset(&out, 0, sizeof(out));

    TEST_ASSERT_FALSE(mspParseByte('$', out));
    TEST_ASSERT_FALSE(mspParseByte('M', out));
    TEST_ASSERT_FALSE(mspParseByte('>', out));
    TEST_ASSERT_FALSE(mspParseByte(4, out));
    TEST_ASSERT_FALSE(mspParseByte(MSP_RC, out));
    TEST_ASSERT_FALSE(mspParseByte(0x01, out));
}

static void test_rc_channel_extraction(void) {
    MspMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.payloadSize = 6;                       // 3 channels
    msg.payload[0] = 0x10; msg.payload[1] = 0x27;  // ch0 = 10000
    msg.payload[2] = 0xE8; msg.payload[3] = 0x03;  // ch1 = 1000
    msg.payload[4] = 0xDC; msg.payload[5] = 0x05;  // ch2 = 1500

    TEST_ASSERT_EQUAL_UINT16(10000, mspGetRcChannel(msg, 0));
    TEST_ASSERT_EQUAL_UINT16(1000,  mspGetRcChannel(msg, 1));
    TEST_ASSERT_EQUAL_UINT16(1500,  mspGetRcChannel(msg, 2));
    // Out of range must be a safe 0, never an out-of-bounds read.
    TEST_ASSERT_EQUAL_UINT16(0, mspGetRcChannel(msg, 3));
    TEST_ASSERT_EQUAL_UINT16(0, mspGetRcChannel(msg, 200));
}

static void test_bounds_readers_fail_closed(void) {
    MspMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.payloadSize = 2;
    msg.payload[0] = 0xFF;
    msg.payload[1] = 0xFF;

    TEST_ASSERT_EQUAL_UINT16(0xFFFF, mspReadU16(msg, 0));
    TEST_ASSERT_EQUAL_UINT16(0, mspReadU16(msg, 1));   // would overrun
    TEST_ASSERT_EQUAL_UINT32(0, mspReadU32(msg, 0));   // would overrun
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_status_response);
    RUN_TEST(test_rejects_bad_crc);
    RUN_TEST(test_resyncs_after_garbage);
    RUN_TEST(test_oversized_payload_dropped);
    RUN_TEST(test_recovers_after_truncated_frame);
    RUN_TEST(test_truncated_frame_emits_nothing);
    RUN_TEST(test_rc_channel_extraction);
    RUN_TEST(test_bounds_readers_fail_closed);
    return UNITY_END();
}

void setup() {}
void loop() {}
