#include <unity.h>
#include "camera_slot_policy.h"

void setUp() {}
void tearDown() {}

void active_visible_camera_has_priority() {
    TEST_ASSERT_EQUAL_INT8(2, cameraNextVisible(0x0f, 0, 4, 2));
}

void powered_off_active_camera_does_not_reserve_a_slot() {
    TEST_ASSERT_EQUAL_INT8(1, cameraNextVisible(0x06, 0, 4, 0));
}

void second_slot_never_duplicates_first_camera() {
    TEST_ASSERT_EQUAL_INT8(2, cameraNextVisible(0x06, 0x02, 4, 1));
    TEST_ASSERT_EQUAL_INT8(-1, cameraNextVisible(0x06, 0x06, 4, 1));
}

void unknown_and_out_of_range_cameras_are_ignored() {
    TEST_ASSERT_EQUAL_INT8(-1, cameraNextVisible(0x80, 0, 4, 7));
    TEST_ASSERT_EQUAL_INT8(-1, cameraNextVisible(0xff, 0, 0, -1));
    TEST_ASSERT_EQUAL_INT8(-1, cameraNextVisible(0, 0, 4, -1));
}

void retry_backoff_is_exponential_and_capped() {
    TEST_ASSERT_EQUAL_UINT32(2000, cameraRetryBackoff(1000, 8000));
    TEST_ASSERT_EQUAL_UINT32(4000, cameraRetryBackoff(2000, 8000));
    TEST_ASSERT_EQUAL_UINT32(8000, cameraRetryBackoff(4000, 8000));
    TEST_ASSERT_EQUAL_UINT32(8000, cameraRetryBackoff(8000, 8000));
    TEST_ASSERT_EQUAL_UINT32(8000, cameraRetryBackoff(0xffffffffUL, 8000));
    TEST_ASSERT_EQUAL_UINT32(6, cameraRetryBackoff(3, 7));
    TEST_ASSERT_EQUAL_UINT32(7, cameraRetryBackoff(4, 7));
    TEST_ASSERT_EQUAL_UINT32(0xfffffffeUL, cameraRetryBackoff(0x7fffffffUL, 0xffffffffUL));
    TEST_ASSERT_EQUAL_UINT32(0xffffffffUL, cameraRetryBackoff(0x80000000UL, 0xffffffffUL));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(active_visible_camera_has_priority);
    RUN_TEST(powered_off_active_camera_does_not_reserve_a_slot);
    RUN_TEST(second_slot_never_duplicates_first_camera);
    RUN_TEST(unknown_and_out_of_range_cameras_are_ignored);
    RUN_TEST(retry_backoff_is_exponential_and_capped);
    return UNITY_END();
}
