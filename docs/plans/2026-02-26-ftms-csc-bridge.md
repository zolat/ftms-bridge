# FTMS-to-CSC BLE Bridge Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build ESP32 firmware that reads FTMS Indoor Bike Data from a Lifespan SM-420 spin bike and re-broadcasts it as standard CSC and CP BLE profiles for Apple Watch pairing.

**Architecture:** The ESP32 runs dual BLE roles — a client that connects to the SM-420's FTMS service and subscribes to Indoor Bike Data notifications, and a server that advertises CSC (0x1816) and CP (0x1818) services. A pure-C++ parsing/accumulation layer (testable on host) sits between the BLE client and server.

**Tech Stack:** PlatformIO + Arduino framework, NimBLE-Arduino ^1.4.0, Unity test framework (native)

---

## File Structure

```
cycle-bridge/
├── platformio.ini
├── include/
│   └── ftms_bridge.h        # Pure C++ parsing & accumulation (no Arduino deps)
├── src/
│   └── main.cpp              # BLE client+server orchestration
└── test/
    └── test_native/
        └── test_main.cpp     # Unity tests for parser & accumulator
```

**Key design choice:** All data parsing and CSC counter logic lives in `ftms_bridge.h` as a header-only library with zero Arduino/NimBLE dependencies. This enables native host-side unit testing via `pio test -e native`. The BLE orchestration in `main.cpp` is tested by flashing and verifying serial output on hardware.

---

### Task 1: Project Scaffolding

**Files:**
- Create: `platformio.ini`
- Create: `include/ftms_bridge.h`
- Create: `src/main.cpp`
- Create: `test/test_native/test_main.cpp`

**Step 1: Create platformio.ini**

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    h2zero/NimBLE-Arduino@^1.4.0
build_flags =
    -D CONFIG_BT_NIMBLE_ROLE_CENTRAL=1
    -D CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=1
    -D CONFIG_BT_NIMBLE_ROLE_OBSERVER=1
    -D CONFIG_BT_NIMBLE_ROLE_BROADCASTER=1
    -D DEBUG_LOG=1

[env:native]
platform = native
build_flags = -std=c++17
test_build_src = false
```

**Step 2: Create minimal ftms_bridge.h**

```cpp
#ifndef FTMS_BRIDGE_H
#define FTMS_BRIDGE_H

#include <cstdint>
#include <cstddef>
#include <cstring>

// ── FTMS Indoor Bike Data Parser ──────────────────────────────────

struct FtmsIndoorBikeData {
    bool hasSpeed;
    bool hasAvgSpeed;
    bool hasCadence;
    bool hasAvgCadence;
    bool hasTotalDistance;
    bool hasResistance;
    bool hasPower;
    bool hasAvgPower;

    uint16_t instantSpeedRaw;    // km/h * 100
    uint16_t avgSpeedRaw;        // km/h * 100
    uint16_t instantCadenceRaw;  // rpm * 2
    uint16_t avgCadenceRaw;      // rpm * 2
    uint32_t totalDistance;       // metres (from uint24)
    int16_t  resistanceLevel;    // unitless
    int16_t  instantPower;       // watts
    int16_t  avgPower;           // watts

    float speedKmh()    const { return instantSpeedRaw / 100.0f; }
    float cadenceRpm()  const { return instantCadenceRaw / 2.0f; }
};

// ── CSC Accumulator ───────────────────────────────────────────────

struct CscAccumulator {
    uint32_t cumulativeWheelRevs = 0;
    uint16_t lastWheelEventTime  = 0;   // 1/1024 sec, wraps at 65536
    uint16_t cumulativeCrankRevs = 0;
    uint16_t lastCrankEventTime  = 0;   // 1/1024 sec, wraps at 65536

    float fractionalWheelRevs = 0.0f;
    float fractionalCrankRevs = 0.0f;

    static constexpr float WHEEL_CIRCUMFERENCE_M = 2.096f;  // 700x23c
};

// ── Function declarations (defined below) ─────────────────────────

inline bool   parseFtmsIndoorBikeData(const uint8_t* data, size_t len, FtmsIndoorBikeData& out);
inline void   updateCsc(CscAccumulator& csc, float speedKmh, float cadenceRpm, uint32_t deltaMs);
inline size_t buildCscMeasurement(uint8_t* buf, const CscAccumulator& csc, bool includeWheel, bool includeCrank);
inline size_t buildCpMeasurement(uint8_t* buf, int16_t powerWatts);

#endif // FTMS_BRIDGE_H
```

(Function bodies will be added in Tasks 2-4.)

**Step 3: Create minimal main.cpp**

```cpp
#include <Arduino.h>

#if DEBUG_LOG
  #define LOG(fmt, ...) Serial.printf(fmt "\n", ##__VA_ARGS__)
#else
  #define LOG(fmt, ...)
#endif

void setup() {
    Serial.begin(115200);
    LOG("SM420 Bridge starting...");
}

void loop() {
    delay(1000);
}
```

**Step 4: Create test skeleton**

```cpp
#include <unity.h>
#include "ftms_bridge.h"

void setUp() {}
void tearDown() {}

void test_placeholder() {
    TEST_ASSERT_TRUE(true);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder);
    return UNITY_END();
}
```

**Step 5: Verify ESP32 build compiles**

Run: `pio run -e esp32dev`
Expected: BUILD SUCCESS

**Step 6: Verify native tests run**

Run: `pio test -e native`
Expected: 1 test passed

**Step 7: Commit**

```bash
git init
git add platformio.ini include/ src/ test/
git commit -m "feat: project scaffolding with platformio, nimble, and native test env"
```

---

### Task 2: FTMS Indoor Bike Data Parser (TDD)

**Files:**
- Modify: `test/test_native/test_main.cpp`
- Modify: `include/ftms_bridge.h`

**Step 1: Write failing tests for FTMS parser**

Add these tests to `test/test_native/test_main.cpp` (replace the placeholder):

```cpp
#include <unity.h>
#include "ftms_bridge.h"

void setUp() {}
void tearDown() {}

// ── FTMS Parser Tests ─────────────────────────────────────────────

void test_parse_too_short() {
    uint8_t data[] = {0x00};
    FtmsIndoorBikeData result;
    TEST_ASSERT_FALSE(parseFtmsIndoorBikeData(data, 1, result));
}

void test_parse_speed_only() {
    // flags=0x0000: bit 0 is 0 → speed IS present (inverted flag)
    // speed=1000 → 10.00 km/h
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
    // flags=0x0001: bit 0 is 1 → speed NOT present
    uint8_t data[] = {0x01, 0x00};
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_FALSE(result.hasSpeed);
}

void test_parse_speed_and_cadence() {
    // flags=0x0004: bit 0=0 (speed present), bit 2=1 (cadence present)
    // speed=2500 (25.00 km/h), cadence=160 (80 rpm)
    uint8_t data[] = {0x04, 0x00,  0xC4, 0x09,  0xA0, 0x00};
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_TRUE(result.hasSpeed);
    TEST_ASSERT_EQUAL_UINT16(2500, result.instantSpeedRaw);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, result.speedKmh());
    TEST_ASSERT_TRUE(result.hasCadence);
    TEST_ASSERT_EQUAL_UINT16(160, result.instantCadenceRaw);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, result.cadenceRpm());
}

void test_parse_speed_cadence_power() {
    // flags=0x0044: bit 0=0 (speed), bit 2=1 (cadence), bit 6=1 (power)
    // speed=3000 (30 km/h), cadence=180 (90 rpm), power=200W
    uint8_t data[] = {0x44, 0x00,  0xB8, 0x0B,  0xB4, 0x00,  0xC8, 0x00};
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_TRUE(result.hasSpeed);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, result.speedKmh());
    TEST_ASSERT_TRUE(result.hasCadence);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, result.cadenceRpm());
    TEST_ASSERT_TRUE(result.hasPower);
    TEST_ASSERT_EQUAL_INT16(200, result.instantPower);
}

void test_parse_all_fields() {
    // flags=0x00FF: all bits 1-7 set, bit 0=1 (no speed)
    // avg_speed=2000, cadence=120, avg_cadence=118,
    // distance=0x001234 (4660m), resistance=10, power=150, avg_power=145
    uint8_t data[] = {
        0xFF, 0x00,
        0xD0, 0x07,             // avg speed = 2000
        0x78, 0x00,             // cadence = 120 (60 rpm)
        0x76, 0x00,             // avg cadence = 118
        0x34, 0x12, 0x00,       // distance = 0x001234 = 4660
        0x0A, 0x00,             // resistance = 10
        0x96, 0x00,             // power = 150
        0x91, 0x00,             // avg power = 145
    };
    FtmsIndoorBikeData result;
    TEST_ASSERT_TRUE(parseFtmsIndoorBikeData(data, sizeof(data), result));
    TEST_ASSERT_FALSE(result.hasSpeed);  // bit 0 = 1 → no speed
    TEST_ASSERT_TRUE(result.hasAvgSpeed);
    TEST_ASSERT_EQUAL_UINT16(2000, result.avgSpeedRaw);
    TEST_ASSERT_TRUE(result.hasCadence);
    TEST_ASSERT_EQUAL_UINT16(120, result.instantCadenceRaw);
    TEST_ASSERT_TRUE(result.hasAvgCadence);
    TEST_ASSERT_EQUAL_UINT16(118, result.avgCadenceRaw);
    TEST_ASSERT_TRUE(result.hasTotalDistance);
    TEST_ASSERT_EQUAL_UINT32(4660, result.totalDistance);
    TEST_ASSERT_TRUE(result.hasResistance);
    TEST_ASSERT_EQUAL_INT16(10, result.resistanceLevel);
    TEST_ASSERT_TRUE(result.hasPower);
    TEST_ASSERT_EQUAL_INT16(150, result.instantPower);
    TEST_ASSERT_TRUE(result.hasAvgPower);
    TEST_ASSERT_EQUAL_INT16(145, result.avgPower);
}

void test_parse_truncated_data() {
    // flags say cadence present but data too short
    uint8_t data[] = {0x04, 0x00,  0xE8, 0x03};  // speed ok, cadence missing
    FtmsIndoorBikeData result;
    TEST_ASSERT_FALSE(parseFtmsIndoorBikeData(data, sizeof(data), result));
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
    return UNITY_END();
}
```

**Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL — linker errors, functions not defined

**Step 3: Implement parseFtmsIndoorBikeData in ftms_bridge.h**

Add this inline definition to `ftms_bridge.h` (before the `#endif`):

```cpp
inline bool parseFtmsIndoorBikeData(const uint8_t* data, size_t len, FtmsIndoorBikeData& out) {
    if (len < 2) return false;

    memset(&out, 0, sizeof(out));
    uint16_t flags = data[0] | (data[1] << 8);
    size_t pos = 2;

    // Bit 0: "More Data" — INVERTED. 0 = speed present, 1 = not present.
    out.hasSpeed = !(flags & 0x0001);
    if (out.hasSpeed) {
        if (pos + 2 > len) return false;
        out.instantSpeedRaw = data[pos] | (data[pos + 1] << 8);
        pos += 2;
    }

    // Bit 1: Average Speed
    out.hasAvgSpeed = flags & 0x0002;
    if (out.hasAvgSpeed) {
        if (pos + 2 > len) return false;
        out.avgSpeedRaw = data[pos] | (data[pos + 1] << 8);
        pos += 2;
    }

    // Bit 2: Instantaneous Cadence
    out.hasCadence = flags & 0x0004;
    if (out.hasCadence) {
        if (pos + 2 > len) return false;
        out.instantCadenceRaw = data[pos] | (data[pos + 1] << 8);
        pos += 2;
    }

    // Bit 3: Average Cadence
    out.hasAvgCadence = flags & 0x0008;
    if (out.hasAvgCadence) {
        if (pos + 2 > len) return false;
        out.avgCadenceRaw = data[pos] | (data[pos + 1] << 8);
        pos += 2;
    }

    // Bit 4: Total Distance (uint24, 3 bytes)
    out.hasTotalDistance = flags & 0x0010;
    if (out.hasTotalDistance) {
        if (pos + 3 > len) return false;
        out.totalDistance = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16);
        pos += 3;
    }

    // Bit 5: Resistance Level (int16)
    out.hasResistance = flags & 0x0020;
    if (out.hasResistance) {
        if (pos + 2 > len) return false;
        out.resistanceLevel = (int16_t)(data[pos] | (data[pos + 1] << 8));
        pos += 2;
    }

    // Bit 6: Instantaneous Power (int16)
    out.hasPower = flags & 0x0040;
    if (out.hasPower) {
        if (pos + 2 > len) return false;
        out.instantPower = (int16_t)(data[pos] | (data[pos + 1] << 8));
        pos += 2;
    }

    // Bit 7: Average Power (int16)
    out.hasAvgPower = flags & 0x0080;
    if (out.hasAvgPower) {
        if (pos + 2 > len) return false;
        out.avgPower = (int16_t)(data[pos] | (data[pos + 1] << 8));
        pos += 2;
    }

    return true;
}
```

**Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: 7 tests passed

**Step 5: Commit**

```bash
git add include/ftms_bridge.h test/test_native/test_main.cpp
git commit -m "feat: FTMS Indoor Bike Data parser with tests"
```

---

### Task 3: CSC Accumulator (TDD)

**Files:**
- Modify: `test/test_native/test_main.cpp`
- Modify: `include/ftms_bridge.h`

**Step 1: Add failing tests for CSC accumulator**

Append to the test file (before `main`):

```cpp
// ── CSC Accumulator Tests ─────────────────────────────────────────

void test_csc_zero_input_no_change() {
    CscAccumulator csc;
    updateCsc(csc, 0.0f, 0.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, csc.cumulativeWheelRevs);
    TEST_ASSERT_EQUAL_UINT16(0, csc.cumulativeCrankRevs);
}

void test_csc_cadence_accumulation() {
    CscAccumulator csc;
    // 60 RPM = 1 rev/sec. After 1 second, should have 1 crank rev.
    updateCsc(csc, 0.0f, 60.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(1, csc.cumulativeCrankRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, csc.fractionalCrankRevs);
}

void test_csc_cadence_fractional() {
    CscAccumulator csc;
    // 80 RPM = 1.333 rev/sec. After 1 second: 1 whole rev, ~0.333 fractional.
    updateCsc(csc, 0.0f, 80.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(1, csc.cumulativeCrankRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.333f, csc.fractionalCrankRevs);

    // After another second: total ~2.666 → 2 more whole revs (3 total), ~0.666 frac
    updateCsc(csc, 0.0f, 80.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(2, csc.cumulativeCrankRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.666f, csc.fractionalCrankRevs);
}

void test_csc_speed_to_wheel_revs() {
    CscAccumulator csc;
    // 30 km/h = 8.333 m/s. In 1 second = 8.333m.
    // Wheel circumference = 2.096m. Revs = 8.333 / 2.096 = 3.975
    updateCsc(csc, 30.0f, 0.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(3, csc.cumulativeWheelRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.975f, csc.fractionalWheelRevs);
}

void test_csc_crank_event_time_advances() {
    CscAccumulator csc;
    // 60 RPM → period = 1.0 sec = 1024 ticks per revolution
    updateCsc(csc, 0.0f, 60.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(1024, csc.lastCrankEventTime);

    updateCsc(csc, 0.0f, 60.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(2048, csc.lastCrankEventTime);
}

void test_csc_wheel_event_time_advances() {
    CscAccumulator csc;
    // 30 km/h = 8.333 m/s. Wheel = 2.096m. Revs/sec = 3.975.
    // Period per rev = 1/3.975 = 0.2516 sec = 257 ticks (approx)
    updateCsc(csc, 30.0f, 0.0f, 1000);
    // 3 revolutions at ~257 ticks each = ~771 ticks
    uint16_t expected = (uint16_t)(3 * (1.0f / (8.333f / 2.096f) * 1024.0f));
    TEST_ASSERT_UINT16_WITHIN(5, expected, csc.lastWheelEventTime);
}

void test_csc_counters_wrap() {
    CscAccumulator csc;
    csc.cumulativeCrankRevs = 65535;
    // 60 RPM, 1 second → +1 rev → wraps to 0
    updateCsc(csc, 0.0f, 60.0f, 1000);
    TEST_ASSERT_EQUAL_UINT16(0, csc.cumulativeCrankRevs);
}
```

Add to `main()`:

```cpp
RUN_TEST(test_csc_zero_input_no_change);
RUN_TEST(test_csc_cadence_accumulation);
RUN_TEST(test_csc_cadence_fractional);
RUN_TEST(test_csc_speed_to_wheel_revs);
RUN_TEST(test_csc_crank_event_time_advances);
RUN_TEST(test_csc_wheel_event_time_advances);
RUN_TEST(test_csc_counters_wrap);
```

**Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL — `updateCsc` not defined

**Step 3: Implement updateCsc in ftms_bridge.h**

```cpp
inline void updateCsc(CscAccumulator& csc, float speedKmh, float cadenceRpm, uint32_t deltaMs) {
    float deltaSec = deltaMs / 1000.0f;

    // ── Wheel revolutions from speed ──
    if (speedKmh > 0.0f) {
        float speedMs = speedKmh / 3.6f;
        float distanceM = speedMs * deltaSec;
        csc.fractionalWheelRevs += distanceM / CscAccumulator::WHEEL_CIRCUMFERENCE_M;

        float wheelRevsPerSec = speedMs / CscAccumulator::WHEEL_CIRCUMFERENCE_M;
        uint16_t periodTicks = (uint16_t)(1.0f / wheelRevsPerSec * 1024.0f);

        while (csc.fractionalWheelRevs >= 1.0f) {
            csc.cumulativeWheelRevs++;
            csc.fractionalWheelRevs -= 1.0f;
            csc.lastWheelEventTime += periodTicks;
        }
    }

    // ── Crank revolutions from cadence ──
    if (cadenceRpm > 0.0f) {
        float revsPerSec = cadenceRpm / 60.0f;
        csc.fractionalCrankRevs += revsPerSec * deltaSec;

        uint16_t periodTicks = (uint16_t)(60.0f / cadenceRpm * 1024.0f);

        while (csc.fractionalCrankRevs >= 1.0f) {
            csc.cumulativeCrankRevs++;
            csc.fractionalCrankRevs -= 1.0f;
            csc.lastCrankEventTime += periodTicks;
        }
    }
}
```

**Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: 14 tests passed

**Step 5: Commit**

```bash
git add include/ftms_bridge.h test/test_native/test_main.cpp
git commit -m "feat: CSC accumulator with wheel and crank revolution tracking"
```

---

### Task 4: CSC and CP Packet Builders (TDD)

**Files:**
- Modify: `test/test_native/test_main.cpp`
- Modify: `include/ftms_bridge.h`

**Step 1: Add failing tests for packet builders**

```cpp
// ── Packet Builder Tests ──────────────────────────────────────────

void test_build_csc_crank_only() {
    CscAccumulator csc;
    csc.cumulativeCrankRevs = 42;
    csc.lastCrankEventTime = 5120;  // 5 seconds in 1/1024
    uint8_t buf[16];
    size_t len = buildCscMeasurement(buf, csc, false, true);
    TEST_ASSERT_EQUAL(5, len);  // 1 flags + 2 crank revs + 2 event time
    TEST_ASSERT_EQUAL(0x02, buf[0]);  // crank flag only
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
    TEST_ASSERT_EQUAL(11, len);  // 1 + 4 + 2 + 2 + 2
    TEST_ASSERT_EQUAL(0x03, buf[0]);  // both flags
    // Wheel revs (uint32 LE)
    uint32_t wheelRevs = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);
    TEST_ASSERT_EQUAL_UINT32(1000, wheelRevs);
    // Wheel event time
    TEST_ASSERT_EQUAL(2048, buf[5] | (buf[6] << 8));
    // Crank revs
    TEST_ASSERT_EQUAL(100, buf[7] | (buf[8] << 8));
    // Crank event time
    TEST_ASSERT_EQUAL(4096, buf[9] | (buf[10] << 8));
}

void test_build_cp_measurement() {
    uint8_t buf[8];
    size_t len = buildCpMeasurement(buf, 200);
    TEST_ASSERT_EQUAL(4, len);  // 2 flags + 2 power
    TEST_ASSERT_EQUAL(0x00, buf[0]);  // flags low
    TEST_ASSERT_EQUAL(0x00, buf[1]);  // flags high
    int16_t power = (int16_t)(buf[2] | (buf[3] << 8));
    TEST_ASSERT_EQUAL_INT16(200, power);
}

void test_build_cp_negative_power() {
    // Edge case: should not happen in practice but test int16 encoding
    uint8_t buf[8];
    buildCpMeasurement(buf, -10);
    int16_t power = (int16_t)(buf[2] | (buf[3] << 8));
    TEST_ASSERT_EQUAL_INT16(-10, power);
}
```

Add to `main()`:

```cpp
RUN_TEST(test_build_csc_crank_only);
RUN_TEST(test_build_csc_wheel_and_crank);
RUN_TEST(test_build_cp_measurement);
RUN_TEST(test_build_cp_negative_power);
```

**Step 2: Run tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL

**Step 3: Implement packet builders in ftms_bridge.h**

```cpp
inline size_t buildCscMeasurement(uint8_t* buf, const CscAccumulator& csc,
                                   bool includeWheel, bool includeCrank) {
    size_t pos = 0;
    uint8_t flags = 0;
    if (includeWheel) flags |= 0x01;
    if (includeCrank) flags |= 0x02;
    buf[pos++] = flags;

    if (includeWheel) {
        buf[pos++] =  csc.cumulativeWheelRevs        & 0xFF;
        buf[pos++] = (csc.cumulativeWheelRevs >> 8)  & 0xFF;
        buf[pos++] = (csc.cumulativeWheelRevs >> 16) & 0xFF;
        buf[pos++] = (csc.cumulativeWheelRevs >> 24) & 0xFF;
        buf[pos++] =  csc.lastWheelEventTime        & 0xFF;
        buf[pos++] = (csc.lastWheelEventTime >> 8)  & 0xFF;
    }

    if (includeCrank) {
        buf[pos++] =  csc.cumulativeCrankRevs        & 0xFF;
        buf[pos++] = (csc.cumulativeCrankRevs >> 8)  & 0xFF;
        buf[pos++] =  csc.lastCrankEventTime         & 0xFF;
        buf[pos++] = (csc.lastCrankEventTime >> 8)   & 0xFF;
    }

    return pos;
}

inline size_t buildCpMeasurement(uint8_t* buf, int16_t powerWatts) {
    // Flags: 0x0000 (no optional fields)
    buf[0] = 0x00;
    buf[1] = 0x00;
    // Instantaneous Power (int16 LE)
    buf[2] = (uint8_t)( powerWatts       & 0xFF);
    buf[3] = (uint8_t)((powerWatts >> 8) & 0xFF);
    return 4;
}
```

**Step 4: Run tests to verify they pass**

Run: `pio test -e native`
Expected: 18 tests passed

**Step 5: Verify ESP32 build still compiles**

Run: `pio run -e esp32dev`
Expected: BUILD SUCCESS

**Step 6: Commit**

```bash
git add include/ftms_bridge.h test/test_native/test_main.cpp
git commit -m "feat: CSC and CP packet builders with tests"
```

---

### Task 5: BLE Client — Scan & Connect to SM-420

**Files:**
- Modify: `src/main.cpp`

This task is tested by flashing to ESP32 and verifying serial output with the bike powered on.

**Step 1: Implement BLE client with FTMS scanning and connection**

Replace `src/main.cpp` with:

```cpp
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ftms_bridge.h"

#if DEBUG_LOG
  #define LOG(fmt, ...) Serial.printf(fmt "\n", ##__VA_ARGS__)
#else
  #define LOG(fmt, ...)
#endif

// ── FTMS UUIDs ────────────────────────────────────────────────────
static const NimBLEUUID FTMS_SERVICE_UUID((uint16_t)0x1826);
static const NimBLEUUID FTMS_INDOOR_BIKE_UUID((uint16_t)0x2AD2);

// ── Shared state (written by BLE callback, read by loop) ─────────
static volatile bool     g_ftmsConnected  = false;
static volatile bool     g_ftmsDataReady  = false;
static volatile uint16_t g_speedRaw       = 0;
static volatile uint16_t g_cadenceRaw     = 0;
static volatile int16_t  g_power          = 0;
static volatile bool     g_hasSpeed       = false;
static volatile bool     g_hasCadence     = false;
static volatile bool     g_hasPower       = false;

// ── Forward declarations ──────────────────────────────────────────
static NimBLEAdvertisedDevice* g_targetDevice = nullptr;
static bool g_doConnect = false;
static NimBLEClient* g_pClient = nullptr;

// ── FTMS notification callback ────────────────────────────────────
static void ftmsNotifyCallback(NimBLERemoteCharacteristic* pChar,
                                uint8_t* pData, size_t length, bool isNotify) {
    FtmsIndoorBikeData bikeData;
    if (!parseFtmsIndoorBikeData(pData, length, bikeData)) {
        LOG("FTMS: parse failed (%d bytes)", length);
        return;
    }

    g_hasSpeed   = bikeData.hasSpeed;
    g_hasCadence = bikeData.hasCadence;
    g_hasPower   = bikeData.hasPower;
    g_speedRaw   = bikeData.instantSpeedRaw;
    g_cadenceRaw = bikeData.instantCadenceRaw;
    g_power      = bikeData.instantPower;
    g_ftmsDataReady = true;

    LOG("FTMS: spd=%.1f km/h  cad=%.0f rpm  pwr=%d W",
        bikeData.hasSpeed   ? bikeData.speedKmh()   : 0.0f,
        bikeData.hasCadence ? bikeData.cadenceRpm()  : 0.0f,
        bikeData.hasPower   ? bikeData.instantPower  : 0);
}

// ── Scan callbacks ────────────────────────────────────────────────
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* device) override {
        LOG("Scan: found %s  RSSI=%d", device->getName().c_str(), device->getRSSI());
        if (device->isAdvertisingService(FTMS_SERVICE_UUID)) {
            LOG("Scan: FTMS device found! Stopping scan.");
            NimBLEDevice::getScan()->stop();
            g_targetDevice = device;
            g_doConnect = true;
        }
    }
};

// ── Client callbacks ──────────────────────────────────────────────
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        LOG("Client: connected to SM-420");
        g_ftmsConnected = true;
    }
    void onDisconnect(NimBLEClient* pClient) override {
        LOG("Client: disconnected from SM-420");
        g_ftmsConnected = false;
        g_ftmsDataReady = false;
    }
};

// ── Connect to FTMS device ────────────────────────────────────────
static bool connectToFtms() {
    if (!g_targetDevice) return false;

    LOG("Connecting to %s...", g_targetDevice->getAddress().toString().c_str());

    if (!g_pClient) {
        g_pClient = NimBLEDevice::createClient();
        g_pClient->setClientCallbacks(new ClientCallbacks());
    }

    if (!g_pClient->connect(g_targetDevice)) {
        LOG("Connection failed!");
        return false;
    }

    NimBLERemoteService* pService = g_pClient->getService(FTMS_SERVICE_UUID);
    if (!pService) {
        LOG("FTMS service not found!");
        g_pClient->disconnect();
        return false;
    }

    NimBLERemoteCharacteristic* pChar = pService->getCharacteristic(FTMS_INDOOR_BIKE_UUID);
    if (!pChar) {
        LOG("Indoor Bike Data characteristic not found!");
        g_pClient->disconnect();
        return false;
    }

    if (!pChar->subscribe(true, ftmsNotifyCallback)) {
        LOG("Failed to subscribe to notifications!");
        g_pClient->disconnect();
        return false;
    }

    LOG("Subscribed to FTMS Indoor Bike Data");
    return true;
}

// ── Scan for FTMS devices ─────────────────────────────────────────
static void startScan() {
    LOG("Scanning for FTMS devices...");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), false);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->start(10, false);  // 10 seconds
}

void setup() {
    Serial.begin(115200);
    LOG("SM420 Bridge starting...");

    NimBLEDevice::init("SM420 Bridge");
    startScan();
}

void loop() {
    // Handle connection request from scan callback
    if (g_doConnect) {
        g_doConnect = false;
        connectToFtms();
    }

    // Re-scan if not connected
    if (!g_ftmsConnected && !NimBLEDevice::getScan()->isScanning()) {
        delay(5000);
        startScan();
    }

    delay(100);
}
```

**Step 2: Verify it compiles**

Run: `pio run -e esp32dev`
Expected: BUILD SUCCESS

**Step 3: Flash and verify on hardware (requires bike powered on)**

Run: `pio run -e esp32dev -t upload && pio device monitor`

Expected serial output:
```
SM420 Bridge starting...
Scanning for FTMS devices...
Scan: found SM-420  RSSI=-65
Scan: FTMS device found! Stopping scan.
Connecting to XX:XX:XX:XX:XX:XX...
Client: connected to SM-420
Subscribed to FTMS Indoor Bike Data
FTMS: spd=0.0 km/h  cad=0 rpm  pwr=0 W
```

Pedal the bike and confirm decoded values appear on serial.

**Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: BLE client scans and connects to FTMS bike, logs parsed data"
```

---

### Task 6: BLE Server — CSC Service + Notifications

**Files:**
- Modify: `src/main.cpp`

**Step 1: Add CSC server alongside existing FTMS client**

Add these UUIDs and globals near the top of `main.cpp`:

```cpp
// ── CSC UUIDs ─────────────────────────────────────────────────────
static const NimBLEUUID CSC_SERVICE_UUID((uint16_t)0x1816);
static const NimBLEUUID CSC_MEASUREMENT_UUID((uint16_t)0x2A5B);
static const NimBLEUUID CSC_FEATURE_UUID((uint16_t)0x2A5C);
static const NimBLEUUID SENSOR_LOCATION_UUID((uint16_t)0x2A5D);

// ── Server state ──────────────────────────────────────────────────
static NimBLEServer*         g_pServer     = nullptr;
static NimBLECharacteristic* g_pCscMeas    = nullptr;
static volatile bool         g_watchConnected = false;
static CscAccumulator        g_csc;
```

Add server callbacks class:

```cpp
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override {
        LOG("Server: Watch connected");
        g_watchConnected = true;
    }
    void onDisconnect(NimBLEServer* pServer) override {
        LOG("Server: Watch disconnected");
        g_watchConnected = false;
        NimBLEDevice::startAdvertising();
    }
};
```

Add server setup function:

```cpp
static void setupBleServer() {
    g_pServer = NimBLEDevice::createServer();
    g_pServer->setCallbacks(new ServerCallbacks());

    // ── CSC Service ───────────────────────────────────────────────
    NimBLEService* pCscService = g_pServer->createService(CSC_SERVICE_UUID);

    // CSC Measurement (notify)
    g_pCscMeas = pCscService->createCharacteristic(
        CSC_MEASUREMENT_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // CSC Feature (read) — bits 0+1: wheel + crank supported
    NimBLECharacteristic* pCscFeature = pCscService->createCharacteristic(
        CSC_FEATURE_UUID,
        NIMBLE_PROPERTY::READ
    );
    uint16_t cscFeatures = 0x0003;  // wheel + crank revolution data supported
    pCscFeature->setValue(cscFeatures);

    // Sensor Location (read) — 0 = "Other"
    NimBLECharacteristic* pSensorLoc = pCscService->createCharacteristic(
        SENSOR_LOCATION_UUID,
        NIMBLE_PROPERTY::READ
    );
    uint8_t sensorLoc = 0x00;
    pSensorLoc->setValue(&sensorLoc, 1);

    pCscService->start();

    // ── Advertising ───────────────────────────────────────────────
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(CSC_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    LOG("Server: advertising CSC service");
}
```

Modify `setup()` — call `setupBleServer()` before `startScan()`:

```cpp
void setup() {
    Serial.begin(115200);
    LOG("SM420 Bridge starting...");

    NimBLEDevice::init("SM420 Bridge");
    setupBleServer();
    startScan();
}
```

Add CSC notification logic to `loop()`:

```cpp
static unsigned long g_lastNotify = 0;
static const unsigned long NOTIFY_INTERVAL_MS = 1000;  // 1 Hz

void loop() {
    if (g_doConnect) {
        g_doConnect = false;
        connectToFtms();
    }

    if (!g_ftmsConnected && !NimBLEDevice::getScan()->isScanning()) {
        delay(5000);
        startScan();
    }

    // ── 1 Hz CSC notifications ────────────────────────────────────
    unsigned long now = millis();
    if (now - g_lastNotify >= NOTIFY_INTERVAL_MS) {
        uint32_t deltaMs = now - g_lastNotify;
        g_lastNotify = now;

        if (g_ftmsDataReady) {
            float speedKmh   = g_hasSpeed   ? (g_speedRaw / 100.0f)  : 0.0f;
            float cadenceRpm  = g_hasCadence ? (g_cadenceRaw / 2.0f)  : 0.0f;

            updateCsc(g_csc, speedKmh, cadenceRpm, deltaMs);

            if (g_watchConnected && g_pCscMeas) {
                uint8_t buf[16];
                size_t len = buildCscMeasurement(buf, g_csc, g_hasSpeed, g_hasCadence);
                g_pCscMeas->setValue(buf, len);
                g_pCscMeas->notify();
                LOG("CSC: wheelRevs=%lu crankRevs=%u",
                    g_csc.cumulativeWheelRevs, g_csc.cumulativeCrankRevs);
            }
        }
    }

    delay(10);
}
```

**Step 2: Verify it compiles**

Run: `pio run -e esp32dev`
Expected: BUILD SUCCESS

**Step 3: Verify native tests still pass**

Run: `pio test -e native`
Expected: 18 tests passed

**Step 4: Flash and verify on hardware**

Run: `pio run -e esp32dev -t upload && pio device monitor`

Expected: CSC service advertises. Pair Apple Watch via Settings → Bluetooth → Health Devices → "SM420 Bridge". With bike pedalling, Watch should show cadence in a workout app.

**Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: BLE server with CSC service, 1Hz notifications from FTMS data"
```

---

### Task 7: Cycling Power Service

**Files:**
- Modify: `src/main.cpp`

**Step 1: Add CP service to BLE server**

Add UUIDs and globals:

```cpp
static const NimBLEUUID CP_SERVICE_UUID((uint16_t)0x1818);
static const NimBLEUUID CP_MEASUREMENT_UUID((uint16_t)0x2A63);
static const NimBLEUUID CP_FEATURE_UUID((uint16_t)0x2A65);

static NimBLECharacteristic* g_pCpMeas = nullptr;
```

Add to `setupBleServer()`, after CSC service setup and before advertising:

```cpp
    // ── Cycling Power Service ─────────────────────────────────────
    NimBLEService* pCpService = g_pServer->createService(CP_SERVICE_UUID);

    g_pCpMeas = pCpService->createCharacteristic(
        CP_MEASUREMENT_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    NimBLECharacteristic* pCpFeature = pCpService->createCharacteristic(
        CP_FEATURE_UUID,
        NIMBLE_PROPERTY::READ
    );
    uint32_t cpFeatures = 0x00000000;  // no optional features
    pCpFeature->setValue(cpFeatures);

    // Sensor Location for CP (required by spec)
    NimBLECharacteristic* pCpSensorLoc = pCpService->createCharacteristic(
        SENSOR_LOCATION_UUID,
        NIMBLE_PROPERTY::READ
    );
    pCpSensorLoc->setValue(&sensorLoc, 1);

    pCpService->start();
```

Add CP service UUID to advertising:

```cpp
    pAdvertising->addServiceUUID(CP_SERVICE_UUID);
```

Add CP notification to the 1Hz loop (inside `if (g_watchConnected)`):

```cpp
            if (g_hasPower && g_pCpMeas) {
                uint8_t cpBuf[8];
                size_t cpLen = buildCpMeasurement(cpBuf, g_power);
                g_pCpMeas->setValue(cpBuf, cpLen);
                g_pCpMeas->notify();
                LOG("CP: power=%d W", g_power);
            }
```

**Step 2: Verify it compiles**

Run: `pio run -e esp32dev`
Expected: BUILD SUCCESS

**Step 3: Flash and verify**

Run: `pio run -e esp32dev -t upload && pio device monitor`

Expected: If SM-420 reports power, Watch shows power data alongside cadence.

**Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add Cycling Power service for FTMS power data"
```

---

### Task 8: Polish — Reconnection, LED Status, Debug Toggle

**Files:**
- Modify: `src/main.cpp`

**Step 1: Add LED status indicators**

Use the onboard LED (GPIO 2 on most ESP32 dev boards):

```cpp
static const int LED_PIN = 2;

// In setup():
    pinMode(LED_PIN, OUTPUT);

// Add to loop() — blink patterns:
// - Slow blink (1Hz): scanning for bike
// - Fast blink (4Hz): connected to bike, waiting for Watch
// - Solid on: bike + Watch connected
static void updateLed() {
    unsigned long now = millis();
    if (g_ftmsConnected && g_watchConnected) {
        digitalWrite(LED_PIN, HIGH);
    } else if (g_ftmsConnected) {
        digitalWrite(LED_PIN, (now / 125) % 2);  // 4Hz blink
    } else {
        digitalWrite(LED_PIN, (now / 500) % 2);  // 1Hz blink
    }
}
```

Call `updateLed()` at the end of `loop()`.

**Step 2: Improve reconnection logic**

Replace the simple re-scan in `loop()` with smarter reconnection. In the `ClientCallbacks::onDisconnect`:

```cpp
    void onDisconnect(NimBLEClient* pClient) override {
        LOG("Client: disconnected from SM-420 — will reconnect");
        g_ftmsConnected = false;
        g_ftmsDataReady = false;
        g_doConnect = true;  // try reconnecting to same device
    }
```

And update the scan-retry logic in `loop()`:

```cpp
    // Reconnect to known device or re-scan
    if (!g_ftmsConnected) {
        if (g_doConnect && g_targetDevice) {
            g_doConnect = false;
            if (!connectToFtms()) {
                LOG("Reconnect failed, will retry in 5s");
                delay(5000);
                g_doConnect = true;
            }
        } else if (!NimBLEDevice::getScan()->isScanning()) {
            delay(5000);
            startScan();
        }
    }
```

**Step 3: Verify it compiles and native tests still pass**

Run: `pio run -e esp32dev && pio test -e native`
Expected: BUILD SUCCESS, 18 tests passed

**Step 4: Flash and verify full behaviour**

Run: `pio run -e esp32dev -t upload && pio device monitor`

Expected:
- LED slow blinks while scanning
- LED fast blinks when bike connected
- LED solid when Watch also connected
- Disconnecting bike → automatic reconnection attempt
- Serial output shows all state transitions

**Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "feat: LED status indicators and auto-reconnect on disconnect"
```

---

## Summary

| Task | What | Test Method |
|------|------|-------------|
| 1 | Project scaffolding | `pio run`, `pio test -e native` |
| 2 | FTMS parser | 7 native unit tests |
| 3 | CSC accumulator | 7 native unit tests |
| 4 | Packet builders | 4 native unit tests |
| 5 | BLE client (FTMS) | Flash + serial output with bike |
| 6 | BLE server (CSC) | Flash + Apple Watch pairing |
| 7 | Cycling Power service | Flash + Apple Watch power display |
| 8 | Polish | Flash + verify LED + reconnect |

**Total native unit tests:** 18
**Estimated lines of code:** ~350 (main.cpp) + ~150 (ftms_bridge.h) + ~150 (tests)
