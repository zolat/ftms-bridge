#include <unity.h>
#include "ftms_bridge.h"

void setUp() {}
void tearDown() {}

void test_parse_too_short() {
    uint8_t data[] = {0x00};
    FtmsIndoorBikeData result;
    TEST_ASSERT_FALSE(parseFtmsIndoorBikeData(data, 1, result));
}

void test_parse_speed_only() {
    uint8_t data[] = {0x00, 0x00, 0xE8, 0x03};
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_TRUE(result.hasSpeed);
    TEST_ASSERT_EQUAL_UINT16(1000, result.instantSpeedRaw);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, result.speedKmh());
    TEST_ASSERT_FALSE(result.hasCadence);
    TEST_ASSERT_FALSE(result.hasPower);
}

void test_parse_no_speed_when_more_data_flag_set() {
    uint8_t data[] = {0x01, 0x00};
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_FALSE(result.hasSpeed);
}

void test_parse_speed_and_cadence() {
    uint8_t data[] = {0x04, 0x00, 0xC4, 0x09, 0xA0, 0x00};
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_TRUE(result.hasSpeed);
    TEST_ASSERT_EQUAL_UINT16(2500, result.instantSpeedRaw);
    TEST_ASSERT_TRUE(result.hasCadence);
    TEST_ASSERT_EQUAL_UINT16(160, result.instantCadenceRaw);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, result.cadenceRpm());
}

void test_parse_speed_cadence_power() {
    uint8_t data[] = {0x44, 0x00, 0xB8, 0x0B, 0xB4, 0x00, 0xC8, 0x00};
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, result.speedKmh());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, result.cadenceRpm());
    TEST_ASSERT_EQUAL_INT16(200, result.instantPower);
}

void test_parse_all_fields() {
    uint8_t data[] = {
        0xFF, 0x00,
        0xD0, 0x07, 0x78, 0x00, 0x76, 0x00,
        0x34, 0x12, 0x00, 0x0A, 0x00, 0x96, 0x00, 0x91, 0x00,
    };
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_FALSE(result.hasSpeed);
    TEST_ASSERT_EQUAL_UINT16(2000, result.avgSpeedRaw);
    TEST_ASSERT_EQUAL_UINT16(120, result.instantCadenceRaw);
    TEST_ASSERT_EQUAL_UINT32(4660, result.totalDistance);
    TEST_ASSERT_EQUAL_INT16(10, result.resistanceLevel);
    TEST_ASSERT_EQUAL_INT16(150, result.instantPower);
    TEST_ASSERT_EQUAL_INT16(145, result.avgPower);
}

void test_parse_truncated_data() {
    uint8_t data[] = {0x04, 0x00, 0xE8, 0x03};
    FtmsIndoorBikeData result;
    TEST_ASSERT_FALSE(parseFtmsIndoorBikeData(data, sizeof(data), result));
}

void test_csc_zero_input_no_change() {
    CscAccumulator csc;
    updateCsc(csc, 0.0f, 0.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, csc.cumulativeWheelRevs);
    TEST_ASSERT_EQUAL_UINT16(0, csc.cumulativeCrankRevs);
}

void test_csc_cadence_accumulation() {
    CscAccumulator csc;
    updateCsc(csc, 0.0f, 60.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(1, csc.cumulativeCrankRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, csc.fractionalCrankRevs);
}

void test_csc_cadence_fractional() {
    CscAccumulator csc;
    updateCsc(csc, 0.0f, 80.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(1, csc.cumulativeCrankRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.333f, csc.fractionalCrankRevs);
    updateCsc(csc, 0.0f, 80.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(2, csc.cumulativeCrankRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.666f, csc.fractionalCrankRevs);
}

void test_csc_speed_to_wheel_revs() {
    CscAccumulator csc;
    updateCsc(csc, 30.0f, 0.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(3, csc.cumulativeWheelRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.975f, csc.fractionalWheelRevs);
}

void test_csc_crank_event_time() {
    CscAccumulator csc;
    updateCsc(csc, 0.0f, 60.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(1024, csc.lastCrankEventTime);
    updateCsc(csc, 0.0f, 60.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(2048, csc.lastCrankEventTime);
}

void test_csc_counters_wrap() {
    CscAccumulator csc;
    csc.cumulativeCrankRevs = 65535;
    updateCsc(csc, 0.0f, 60.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(0, csc.cumulativeCrankRevs);
}

void test_build_csc_crank_only() {
    CscAccumulator csc;
    csc.cumulativeCrankRevs = 42;
    csc.lastCrankEventTime = 5120;
    uint8_t buf[16];
    size_t len = buildCscMeasurement(buf, csc, false, true);
    TEST_ASSERT_EQUAL(5, len);
    TEST_ASSERT_EQUAL(0x02, buf[0]);
    TEST_ASSERT_EQUAL(42, buf[1] | (buf[2] << 8));
    TEST_ASSERT_EQUAL(5120, buf[3] | (buf[4] << 8));
}

void test_build_csc_wheel_and_crank() {
    CscAccumulator csc;
    csc.cumulativeWheelRevs = 1000;
    csc.lastWheelEventTime = 2048;
    csc.cumulativeCrankRevs = 100;
    csc.lastCrankEventTime = 4096;
    uint8_t buf[16];
    size_t len = buildCscMeasurement(buf, csc, true, true);
    TEST_ASSERT_EQUAL(11, len);
    TEST_ASSERT_EQUAL(0x03, buf[0]);
    uint32_t wr = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);
    TEST_ASSERT_EQUAL_UINT32(1000, wr);
}

void test_build_cp_measurement() {
    uint8_t buf[8];
    size_t len = buildCpMeasurement(buf, 200);
    TEST_ASSERT_EQUAL(4, len);
    int16_t power = (int16_t)(buf[2] | (buf[3] << 8));
    TEST_ASSERT_EQUAL_INT16(200, power);
}

void test_csc_custom_wheel_circumference() {
    CscAccumulator csc;
    csc.wheelCircumferenceM = 1.0f;  // 1 meter wheel = easy math
    // 3.6 km/h = 1 m/s, so in 1 second we travel 1m = exactly 1 rev
    updateCsc(csc, 3.6f, 0.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, csc.cumulativeWheelRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, csc.fractionalWheelRevs);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_too_short);
    RUN_TEST(test_parse_speed_only);
    RUN_TEST(test_parse_no_speed_when_more_data_flag_set);
    RUN_TEST(test_parse_speed_and_cadence);
    RUN_TEST(test_parse_speed_cadence_power);
    RUN_TEST(test_parse_all_fields);
    RUN_TEST(test_parse_truncated_data);
    RUN_TEST(test_csc_zero_input_no_change);
    RUN_TEST(test_csc_cadence_accumulation);
    RUN_TEST(test_csc_cadence_fractional);
    RUN_TEST(test_csc_speed_to_wheel_revs);
    RUN_TEST(test_csc_crank_event_time);
    RUN_TEST(test_csc_counters_wrap);
    RUN_TEST(test_build_csc_crank_only);
    RUN_TEST(test_build_csc_wheel_and_crank);
    RUN_TEST(test_build_cp_measurement);
    RUN_TEST(test_csc_custom_wheel_circumference);
    return UNITY_END();
}
