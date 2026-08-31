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

static uint16_t rd16(const uint8_t* b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static uint32_t rd32(const uint8_t* b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

void test_build_cp_power_only() {
    CscAccumulator csc;
    uint8_t buf[16];
    size_t len = buildCpMeasurement(buf, 200, csc, false, false);
    TEST_ASSERT_EQUAL(4, len);
    TEST_ASSERT_EQUAL_UINT16(0x0000, rd16(&buf[0]));
    TEST_ASSERT_EQUAL_INT16(200, (int16_t)rd16(&buf[2]));
}

void test_build_cp_crank_only() {
    CscAccumulator csc;
    csc.cumulativeCrankRevs = 42;
    csc.lastCrankEventTime  = 5120;
    uint8_t buf[16];
    size_t len = buildCpMeasurement(buf, 175, csc, false, true);
    TEST_ASSERT_EQUAL(8, len);
    TEST_ASSERT_EQUAL_UINT16(0x0020, rd16(&buf[0]));
    TEST_ASSERT_EQUAL_INT16(175, (int16_t)rd16(&buf[2]));
    TEST_ASSERT_EQUAL_UINT16(42, rd16(&buf[4]));
    TEST_ASSERT_EQUAL_UINT16(5120, rd16(&buf[6]));
}

void test_build_cp_wheel_and_crank() {
    CscAccumulator csc;
    csc.cumulativeWheelRevs = 100000;
    csc.lastWheelEventTime  = 1000;
    csc.cumulativeCrankRevs = 250;
    csc.lastCrankEventTime  = 4096;
    uint8_t buf[16];
    size_t len = buildCpMeasurement(buf, 300, csc, true, true);
    TEST_ASSERT_EQUAL(14, len);
    TEST_ASSERT_EQUAL_UINT16(0x0030, rd16(&buf[0]));
    TEST_ASSERT_EQUAL_INT16(300, (int16_t)rd16(&buf[2]));
    // Wheel block comes before the crank block.
    TEST_ASSERT_EQUAL_UINT32(100000, rd32(&buf[4]));
    TEST_ASSERT_EQUAL_UINT16(2000, rd16(&buf[8]));
    TEST_ASSERT_EQUAL_UINT16(250, rd16(&buf[10]));
    TEST_ASSERT_EQUAL_UINT16(4096, rd16(&buf[12]));
}

void test_build_cp_wheel_only() {
    CscAccumulator csc;
    csc.cumulativeWheelRevs = 7;
    csc.lastWheelEventTime  = 512;
    uint8_t buf[16];
    size_t len = buildCpMeasurement(buf, 0, csc, true, false);
    TEST_ASSERT_EQUAL(10, len);
    TEST_ASSERT_EQUAL_UINT16(0x0010, rd16(&buf[0]));
    TEST_ASSERT_EQUAL_UINT32(7, rd32(&buf[4]));
    TEST_ASSERT_EQUAL_UINT16(1024, rd16(&buf[8]));
}

// CSC reports the wheel event time in 1/1024 s, CPS in 1/2048 s. Same accumulator,
// same instant, so the CPS value must be exactly double.
void test_build_cp_wheel_time_is_2048hz() {
    CscAccumulator csc;
    csc.cumulativeWheelRevs = 1234;
    csc.lastWheelEventTime  = 30000;

    uint8_t cscBuf[16];
    buildCscMeasurement(cscBuf, csc, true, false);
    uint16_t cscTime = rd16(&cscBuf[5]);

    uint8_t cpBuf[16];
    buildCpMeasurement(cpBuf, 0, csc, true, false);
    uint16_t cpTime = rd16(&cpBuf[8]);

    TEST_ASSERT_EQUAL_UINT16(30000, cscTime);
    TEST_ASSERT_EQUAL_UINT16(60000, cpTime);
}

// Doubling has to stay correct once the 1/2048 s counter has wrapped past uint16.
void test_build_cp_wheel_time_wraps() {
    CscAccumulator csc;
    csc.lastWheelEventTime = 40000;  // x2 = 80000, wraps to 14464
    uint8_t buf[16];
    buildCpMeasurement(buf, 0, csc, true, false);
    TEST_ASSERT_EQUAL_UINT16(14464, rd16(&buf[8]));
}

void test_build_cp_negative_power() {
    CscAccumulator csc;
    uint8_t buf[16];
    buildCpMeasurement(buf, -50, csc, false, false);
    TEST_ASSERT_EQUAL_INT16(-50, (int16_t)rd16(&buf[2]));
}

void test_cp_feature_bits_little_endian() {
    uint8_t buf[4];
    size_t len = serializeCpFeature(buf, CP_FEATURE_WHEEL_REV | CP_FEATURE_CRANK_REV);
    TEST_ASSERT_EQUAL(4, len);
    TEST_ASSERT_EQUAL_UINT8(0x0C, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[3]);
}

// The acceptance test: decode the CPM stream the way a head unit does and check that
// the cadence and speed a Garmin would display match what the bike reported.
void test_cp_cadence_and_speed_recoverable() {
    const float cadenceRpm = 90.0f;
    const float speedKmh   = 30.0f;

    CscAccumulator csc;
    csc.wheelCircumferenceM = 2.096f;

    uint8_t first[16], last[16];
    updateCsc(csc, speedKmh, cadenceRpm, 1000);
    buildCpMeasurement(first, 200, csc, true, true);

    for (int i = 0; i < 10; i++) {
        updateCsc(csc, speedKmh, cadenceRpm, 1000);
    }
    buildCpMeasurement(last, 200, csc, true, true);

    // Cadence = dCrankRevs / dCrankTime(1/1024 s) * 60
    uint16_t dCrankRevs = (uint16_t)(rd16(&last[10]) - rd16(&first[10]));
    uint16_t dCrankTime = (uint16_t)(rd16(&last[12]) - rd16(&first[12]));
    float decodedCadence = (float)dCrankRevs * 1024.0f * 60.0f / (float)dCrankTime;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, cadenceRpm, decodedCadence);

    // Speed = dWheelRevs / dWheelTime(1/2048 s) * circumference
    uint32_t dWheelRevs = rd32(&last[4]) - rd32(&first[4]);
    uint16_t dWheelTime = (uint16_t)(rd16(&last[8]) - rd16(&first[8]));
    float revsPerSec = (float)dWheelRevs * 2048.0f / (float)dWheelTime;
    float decodedKmh = revsPerSec * csc.wheelCircumferenceM * 3.6f;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, speedKmh, decodedKmh);
}

// The whole path in one test: the bytes the bike puts on the wire, through the parser
// and accumulator, out as a Cycling Power notification, decoded the way a watch does.
void test_end_to_end_ftms_to_cp() {
    // Speed 30.00 km/h, cadence 90 rpm, power 200 W.
    uint8_t ftms[] = {0x44, 0x00, 0xB8, 0x0B, 0xB4, 0x00, 0xC8, 0x00};

    CscAccumulator csc;
    csc.wheelCircumferenceM = 2.096f;
    FtmsIndoorBikeData bike;

    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(ftms, sizeof(ftms), bike));
    updateCsc(csc, bike.speedKmh(), bike.cadenceRpm(), 1000);

    uint8_t first[16], last[16];
    buildCpMeasurement(first, bike.instantPower, csc, true, true);

    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(ftms, sizeof(ftms), bike));
        updateCsc(csc, bike.speedKmh(), bike.cadenceRpm(), 1000);
    }
    buildCpMeasurement(last, bike.instantPower, csc, true, true);

    TEST_ASSERT_EQUAL_UINT16(0x0030, rd16(&last[0]));
    TEST_ASSERT_EQUAL_INT16(200, (int16_t)rd16(&last[2]));

    uint16_t dCrankRevs = (uint16_t)(rd16(&last[10]) - rd16(&first[10]));
    uint16_t dCrankTime = (uint16_t)(rd16(&last[12]) - rd16(&first[12]));
    TEST_ASSERT_FLOAT_WITHIN(
        0.5f, 90.0f, (float)dCrankRevs * 1024.0f * 60.0f / (float)dCrankTime);

    uint32_t dWheelRevs = rd32(&last[4]) - rd32(&first[4]);
    uint16_t dWheelTime = (uint16_t)(rd16(&last[8]) - rd16(&first[8]));
    float revsPerSec = (float)dWheelRevs * 2048.0f / (float)dWheelTime;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 30.0f, revsPerSec * csc.wheelCircumferenceM * 3.6f);
}

// A bike that reports no power still has to produce a usable cadence stream, because
// that is the channel a watch paired as a power meter listens to.
void test_cp_notifies_cadence_without_power() {
    CscAccumulator csc;
    updateCsc(csc, 0.0f, 80.0f, 1000);
    uint8_t buf[16];
    size_t len = buildCpMeasurement(buf, 0, csc, false, true);
    TEST_ASSERT_EQUAL(8, len);
    TEST_ASSERT_EQUAL_UINT16(0x0020, rd16(&buf[0]));
    TEST_ASSERT_EQUAL_INT16(0, (int16_t)rd16(&buf[2]));
    TEST_ASSERT_EQUAL_UINT16(1, rd16(&buf[4]));
}

// The SM-420 splits its data across notifications: one carries speed and cadence, the
// next carries power with the "More Data" bit set so instantaneous speed is absent.
// Getting that bit backwards would shift every field that follows.
void test_parse_power_only_notification() {
    uint8_t data[] = {0x41, 0x00, 0xC8, 0x00};  // More Data + instantaneous power
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_FALSE(result.hasSpeed);
    TEST_ASSERT_FALSE(result.hasCadence);
    TEST_ASSERT_TRUE(result.hasPower);
    TEST_ASSERT_EQUAL_INT16(200, result.instantPower);
}

// Sweep the range a rider actually covers and check the numbers a watch computes from
// the Cycling Power stream match what the bike reported. The event times quantise to
// whole 1/1024 s ticks, so a little error is inherent -- this pins how much.
void test_cp_decoded_values_across_range() {
    for (int rpm = 40; rpm <= 130; rpm += 10) {
        CscAccumulator csc;
        csc.wheelCircumferenceM = 2.096f;
        uint8_t first[16], last[16];

        updateCsc(csc, 0.0f, (float)rpm, 1000);
        buildCpMeasurement(first, 0, csc, false, true);
        for (int i = 0; i < 20; i++) updateCsc(csc, 0.0f, (float)rpm, 1000);
        buildCpMeasurement(last, 0, csc, false, true);

        uint16_t dRevs = (uint16_t)(rd16(&last[4]) - rd16(&first[4]));
        uint16_t dTime = (uint16_t)(rd16(&last[6]) - rd16(&first[6]));
        TEST_ASSERT_TRUE(dTime > 0);
        float decoded = (float)dRevs * 1024.0f * 60.0f / (float)dTime;
        TEST_ASSERT_FLOAT_WITHIN(0.5f, (float)rpm, decoded);
    }

    for (int kmh = 5; kmh <= 60; kmh += 5) {
        CscAccumulator csc;
        csc.wheelCircumferenceM = 2.096f;
        uint8_t first[16], last[16];

        updateCsc(csc, (float)kmh, 0.0f, 1000);
        buildCpMeasurement(first, 0, csc, true, false);
        for (int i = 0; i < 20; i++) updateCsc(csc, (float)kmh, 0.0f, 1000);
        buildCpMeasurement(last, 0, csc, true, false);

        uint32_t dRevs = rd32(&last[4]) - rd32(&first[4]);
        uint16_t dTime = (uint16_t)(rd16(&last[8]) - rd16(&first[8]));
        TEST_ASSERT_TRUE(dTime > 0);
        float revsPerSec = (float)dRevs * 2048.0f / (float)dTime;
        float decoded = revsPerSec * csc.wheelCircumferenceM * 3.6f;
        TEST_ASSERT_FLOAT_WITHIN(kmh * 0.01f + 0.05f, (float)kmh, decoded);
    }
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
    RUN_TEST(test_build_cp_power_only);
    RUN_TEST(test_build_cp_crank_only);
    RUN_TEST(test_build_cp_wheel_and_crank);
    RUN_TEST(test_build_cp_wheel_only);
    RUN_TEST(test_build_cp_wheel_time_is_2048hz);
    RUN_TEST(test_build_cp_wheel_time_wraps);
    RUN_TEST(test_build_cp_negative_power);
    RUN_TEST(test_cp_feature_bits_little_endian);
    RUN_TEST(test_cp_cadence_and_speed_recoverable);
    RUN_TEST(test_end_to_end_ftms_to_cp);
    RUN_TEST(test_cp_notifies_cadence_without_power);
    RUN_TEST(test_parse_power_only_notification);
    RUN_TEST(test_cp_decoded_values_across_range);
    RUN_TEST(test_csc_custom_wheel_circumference);
    return UNITY_END();
}
