#include <unity.h>
#include <string.h>
#include "osd_status.h"

void setUp() {}
void tearDown() {}

// Two saved cameras, both connected, idle on the ground.
static OsdStatusInput twoCams() {
    OsdStatusInput in;
    memset(&in, 0, sizeof(in));
    in.camCount = 2;
    strcpy(in.cams[0].tag, "QUAD");
    strcpy(in.cams[1].tag, "STK");
    for (int i = 0; i < 2; i++) {
        in.cams[i].expected = true;
        in.cams[i].ready = true;
        in.cams[i].battery = 255;
    }
    in.cams[0].battery = 85;
    in.cams[1].battery = 62;
    in.fcAlive = true;
    in.sinceRecEdgeMs = 60000;
    in.lowBatteryPct = 15;
    return in;
}

static uint8_t alerts(const OsdStatusInput &in, char out[][OSD_ALERT_TEXT_LEN + 1]) {
    return osdCollectAlerts(in, out, 8);
}

// ── Summary ────────────────────────────────────────────────────────────────

void summary_without_cameras() {
    OsdStatusInput in;
    memset(&in, 0, sizeof(in));
    char buf[17];
    osdFormatSummary(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("CAM NONE", buf);
}

void summary_on_the_ground() {
    OsdStatusInput in = twoCams();
    char buf[17];
    osdFormatSummary(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("CAM 2/2 85 62", buf);
}

void summary_missing_stickcam_shows_dashes() {
    OsdStatusInput in = twoCams();
    in.cams[1].ready = false;
    char buf[17];
    osdFormatSummary(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("CAM 1/2 85 --", buf);
}

void summary_recording_fits_sixteen_chars() {
    OsdStatusInput in = twoCams();
    in.recDesired = true;
    in.cams[0].recording = true; in.cams[0].recSeconds = 161;
    in.cams[1].recording = true; in.cams[1].recSeconds = 150;
    in.cams[0].battery = 100;   // shown as 99 to keep the width
    char buf[17];
    osdFormatSummary(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("R2/2 99 62 02:41", buf);
    TEST_ASSERT_TRUE(strlen(buf) <= 16);
}

void summary_shows_camera_that_is_not_recording() {
    OsdStatusInput in = twoCams();
    in.recDesired = true;
    in.cams[0].recording = true; in.cams[0].recSeconds = 5;
    char buf[17];
    osdFormatSummary(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("R1/2 85 62 00:05", buf);
}

void summary_drops_time_instead_of_truncating() {
    OsdStatusInput in = twoCams();
    in.recDesired = true;
    in.cams[0].recording = true; in.cams[0].recSeconds = 6000;   // 100:00
    in.cams[1].recording = true;
    char buf[17];
    osdFormatSummary(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("R2/2 85 62", buf);
}

void summary_ignores_cameras_without_slot_when_more_are_saved() {
    OsdStatusInput in = twoCams();
    in.camCount = 3;
    in.cams[2].expected = false;   // third saved camera, no slot
    char buf[17];
    osdFormatSummary(in, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("CAM 2/2 85 62", buf);
}

// ── Alerts ─────────────────────────────────────────────────────────────────

void no_alerts_when_everything_is_fine() {
    OsdStatusInput in = twoCams();
    char out[8][OSD_ALERT_TEXT_LEN + 1];
    TEST_ASSERT_EQUAL_UINT8(0, alerts(in, out));
    in.recDesired = true;
    in.cams[0].recording = in.cams[1].recording = true;
    TEST_ASSERT_EQUAL_UINT8(0, alerts(in, out));
}

void not_rec_waits_for_grace_period() {
    OsdStatusInput in = twoCams();
    in.recDesired = true;
    in.cams[0].recording = true;
    in.sinceRecEdgeMs = OSD_NOT_REC_GRACE_MS - 1;
    char out[8][OSD_ALERT_TEXT_LEN + 1];
    TEST_ASSERT_EQUAL_UINT8(0, alerts(in, out));
    in.sinceRecEdgeMs = OSD_NOT_REC_GRACE_MS;
    TEST_ASSERT_EQUAL_UINT8(1, alerts(in, out));
    TEST_ASSERT_EQUAL_STRING("STK NOT REC", out[0]);
}

void rejected_start_is_reported_at_once_and_first() {
    OsdStatusInput in = twoCams();
    in.recDesired = true;
    in.sinceRecEdgeMs = 0;
    in.cams[0].recRejected = true;
    in.cams[1].ready = false;
    char out[8][OSD_ALERT_TEXT_LEN + 1];
    TEST_ASSERT_EQUAL_UINT8(2, alerts(in, out));
    TEST_ASSERT_EQUAL_STRING("QUAD REC FAIL", out[0]);
    TEST_ASSERT_EQUAL_STRING("STK LOST", out[1]);
}

void pending_stop_after_landing() {
    OsdStatusInput in = twoCams();
    in.cams[1].ready = false;
    in.cams[1].stopPending = true;
    in.showAllStopped = true;
    char out[8][OSD_ALERT_TEXT_LEN + 1];
    TEST_ASSERT_EQUAL_UINT8(1, alerts(in, out));
    TEST_ASSERT_EQUAL_STRING("STK STOP WAIT", out[0]);   // no "ALL STOPPED" yet
}

void all_stopped_confirmation() {
    OsdStatusInput in = twoCams();
    in.showAllStopped = true;
    char out[8][OSD_ALERT_TEXT_LEN + 1];
    TEST_ASSERT_EQUAL_UINT8(1, alerts(in, out));
    TEST_ASSERT_EQUAL_STRING("ALL STOPPED", out[0]);
}

void camera_still_recording_after_stop() {
    OsdStatusInput in = twoCams();
    in.cams[0].recording = true;
    char out[8][OSD_ALERT_TEXT_LEN + 1];
    TEST_ASSERT_EQUAL_UINT8(1, alerts(in, out));
    TEST_ASSERT_EQUAL_STRING("QUAD STILL REC", out[0]);
}

void low_battery_and_fc_link() {
    OsdStatusInput in = twoCams();
    in.cams[1].battery = 9;
    in.fcAlive = false;
    char out[8][OSD_ALERT_TEXT_LEN + 1];
    TEST_ASSERT_EQUAL_UINT8(2, alerts(in, out));
    TEST_ASSERT_EQUAL_STRING("STK BAT 9", out[0]);
    TEST_ASSERT_EQUAL_STRING("FC NOLINK", out[1]);
}

void untagged_camera_uses_its_number() {
    OsdStatusInput in = twoCams();
    in.cams[1].tag[0] = '\0';
    in.recDesired = true;
    in.cams[0].recording = true;
    in.cams[1].ready = false;
    char out[8][OSD_ALERT_TEXT_LEN + 1];
    TEST_ASSERT_EQUAL_UINT8(1, alerts(in, out));
    TEST_ASSERT_EQUAL_STRING("C2 LOST", out[0]);
}

void alert_list_respects_capacity() {
    OsdStatusInput in = twoCams();
    in.recDesired = true;
    in.cams[0].ready = in.cams[1].ready = false;
    in.fcAlive = false;
    char out[1][OSD_ALERT_TEXT_LEN + 1];
    TEST_ASSERT_EQUAL_UINT8(1, osdCollectAlerts(in, out, 1));
    TEST_ASSERT_EQUAL_STRING("QUAD LOST", out[0]);
}

// ── Tags ───────────────────────────────────────────────────────────────────

void tag_is_normalised() {
    char t[5];
    TEST_ASSERT_EQUAL_UINT32(3, osdNormaliseTag("st-k", t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING("STK", t);
    osdNormaliseTag("quad cam", t, sizeof(t));
    TEST_ASSERT_EQUAL_STRING("QUAD", t);
    osdNormaliseTag("\"<>", t, sizeof(t));
    TEST_ASSERT_EQUAL_STRING("", t);
    osdNormaliseTag(nullptr, t, sizeof(t));
    TEST_ASSERT_EQUAL_STRING("", t);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(summary_without_cameras);
    RUN_TEST(summary_on_the_ground);
    RUN_TEST(summary_missing_stickcam_shows_dashes);
    RUN_TEST(summary_recording_fits_sixteen_chars);
    RUN_TEST(summary_shows_camera_that_is_not_recording);
    RUN_TEST(summary_drops_time_instead_of_truncating);
    RUN_TEST(summary_ignores_cameras_without_slot_when_more_are_saved);
    RUN_TEST(no_alerts_when_everything_is_fine);
    RUN_TEST(not_rec_waits_for_grace_period);
    RUN_TEST(rejected_start_is_reported_at_once_and_first);
    RUN_TEST(pending_stop_after_landing);
    RUN_TEST(all_stopped_confirmation);
    RUN_TEST(camera_still_recording_after_stop);
    RUN_TEST(low_battery_and_fc_link);
    RUN_TEST(untagged_camera_uses_its_number);
    RUN_TEST(alert_list_respects_capacity);
    RUN_TEST(tag_is_normalised);
    return UNITY_END();
}
