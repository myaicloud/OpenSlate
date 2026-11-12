// ============================================================================
// test_autobind.cpp — Host tests for the auto-bind name classifier
// ============================================================================

#include <unity.h>
#include "autobind_match.h"

void setUp(void) {}
void tearDown(void) {}

static void test_dji_names(void) {
    TEST_ASSERT_EQUAL(AUTOBIND_DJI, autobindClassifyName("Osmo Action 4-1A2B"));
    TEST_ASSERT_EQUAL(AUTOBIND_DJI, autobindClassifyName("DJI Action 2"));
    TEST_ASSERT_EQUAL(AUTOBIND_DJI, autobindClassifyName("dji-xyz"));
    TEST_ASSERT_EQUAL(AUTOBIND_DJI, autobindClassifyName("My ACTION cam"));
}

static void test_gopro_names(void) {
    TEST_ASSERT_EQUAL(AUTOBIND_GOPRO, autobindClassifyName("GoPro 1234"));
    TEST_ASSERT_EQUAL(AUTOBIND_GOPRO, autobindClassifyName("GOPRO"));
    TEST_ASSERT_EQUAL(AUTOBIND_GOPRO, autobindClassifyName("HERO12 Black"));
    TEST_ASSERT_EQUAL(AUTOBIND_GOPRO, autobindClassifyName("GP12345678"));
}

static void test_non_cameras(void) {
    TEST_ASSERT_EQUAL(AUTOBIND_NONE, autobindClassifyName(0));
    TEST_ASSERT_EQUAL(AUTOBIND_NONE, autobindClassifyName(""));
    TEST_ASSERT_EQUAL(AUTOBIND_NONE, autobindClassifyName("g"));
    TEST_ASSERT_EQUAL(AUTOBIND_NONE, autobindClassifyName("JBL Flip 5"));
    TEST_ASSERT_EQUAL(AUTOBIND_NONE, autobindClassifyName("Mi Band 6"));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_dji_names);
    RUN_TEST(test_gopro_names);
    RUN_TEST(test_non_cameras);
    return UNITY_END();
}

void setup() {}
void loop() {}
