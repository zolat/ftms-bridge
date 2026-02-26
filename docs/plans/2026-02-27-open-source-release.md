# Open-Source Release Restructure — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Prepare cycle-bridge for public GitHub release with user-configurable settings, MIT license, and a warm practical README.

**Architecture:** Extract hardcoded constants from `main.cpp` and `ftms_bridge.h` into a new `config.h`. Make wheel circumference a settable member on `CscAccumulator` (with default) so the header-only library stays testable without config.h. Add project metadata files (LICENSE, .gitignore, README).

**Tech Stack:** PlatformIO, ESP32, NimBLE-Arduino, C++17 (native tests)

---

### Task 1: Create config.h

**Files:**
- Create: `include/config.h`

**Step 1: Create the config file**

```cpp
#ifndef CONFIG_H
#define CONFIG_H

// ── Your Bike Settings ──────────────────────────────────────────
// Edit these values for your bike, then build & flash.

// BLE MAC address of your FTMS bike.
// Find it with the nRF Connect app: scan for devices advertising "FTMS".
#define BIKE_MAC         "24:00:0c:a0:7c:60"

// Name your Apple Watch will see when pairing.
#define BRIDGE_NAME      "SM420 Bridge"

// Wheel circumference in millimeters.
// 2096mm = 700x25c road tire. Adjust for your bike/trainer.
#define WHEEL_CIRC_MM    2096

// GPIO pin for the onboard status LED (2 on most ESP32 dev boards).
#define LED_PIN          2

#endif // CONFIG_H
```

**Step 2: Commit**

```bash
git add include/config.h
git commit -m "feat: add user-editable config.h for bike settings"
```

---

### Task 2: Make wheel circumference configurable in ftms_bridge.h

**Files:**
- Modify: `include/ftms_bridge.h:31-39` (CscAccumulator struct)
- Modify: `include/ftms_bridge.h:98-121` (updateCsc function)
- Test: `test/test_native/test_main.cpp`

**Step 1: Write a failing test for custom wheel circumference**

Add to `test/test_native/test_main.cpp` before the `main()` function:

```cpp
void test_csc_custom_wheel_circumference() {
    CscAccumulator csc;
    csc.wheelCircumferenceM = 1.0f;  // 1 meter wheel = easy math
    // 3.6 km/h = 1 m/s, so in 1 second we travel 1m = exactly 1 rev
    updateCsc(csc, 3.6f, 0.0f, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, csc.cumulativeWheelRevs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, csc.fractionalWheelRevs);
}
```

Add `RUN_TEST(test_csc_custom_wheel_circumference);` to `main()`.

**Step 2: Run test to verify it fails**

Run: `cd /Users/thomaszola/cycle-bridge && pio test -e native`
Expected: FAIL — `wheelCircumferenceM` is not a member (it's a static constexpr)

**Step 3: Update CscAccumulator in ftms_bridge.h**

Change the struct (lines 31-39) from:

```cpp
struct CscAccumulator {
    uint32_t cumulativeWheelRevs = 0;
    uint16_t lastWheelEventTime  = 0;
    uint16_t cumulativeCrankRevs = 0;
    uint16_t lastCrankEventTime  = 0;
    float fractionalWheelRevs = 0.0f;
    float fractionalCrankRevs = 0.0f;
    static constexpr float WHEEL_CIRCUMFERENCE_M = 2.096f;
};
```

To:

```cpp
struct CscAccumulator {
    uint32_t cumulativeWheelRevs = 0;
    uint16_t lastWheelEventTime  = 0;
    uint16_t cumulativeCrankRevs = 0;
    uint16_t lastCrankEventTime  = 0;
    float fractionalWheelRevs = 0.0f;
    float fractionalCrankRevs = 0.0f;
    float wheelCircumferenceM = 2.096f;
};
```

**Step 4: Update updateCsc to use the member**

In `updateCsc()` (lines 98-121), replace both occurrences of `CscAccumulator::WHEEL_CIRCUMFERENCE_M` with `csc.wheelCircumferenceM`.

Line 103: `csc.fractionalWheelRevs += distanceM / csc.wheelCircumferenceM;`
Line 104: `float wheelRevsPerSec = speedMs / csc.wheelCircumferenceM;`

**Step 5: Run tests to verify all 17 pass**

Run: `cd /Users/thomaszola/cycle-bridge && pio test -e native`
Expected: 17 tests, 0 failures (existing tests use the 2.096 default, new test uses custom value)

**Step 6: Commit**

```bash
git add include/ftms_bridge.h test/test_native/test_main.cpp
git commit -m "feat: make wheel circumference configurable on CscAccumulator"
```

---

### Task 3: Refactor main.cpp to use config.h

**Files:**
- Modify: `src/main.cpp`

**Step 1: Add config.h include and remove hardcoded values**

At `src/main.cpp:3`, add:
```cpp
#include "config.h"
```

Remove line 45 (`static const int LED_PIN = 2;`) — now comes from config.h.

Change line 82 from:
```cpp
static const NimBLEAddress SM420_ADDRESS("24:00:0c:a0:7c:60");
```
To:
```cpp
static const NimBLEAddress TARGET_ADDRESS(BIKE_MAC);
```

Replace all occurrences of `SM420_ADDRESS` with `TARGET_ADDRESS` (lines 87, 191).

Change line 268 from:
```cpp
NimBLEDevice::init("SM420 Bridge");
```
To:
```cpp
NimBLEDevice::init(BRIDGE_NAME);
```

Update log messages to use `BRIDGE_NAME` instead of hardcoded "SM-420" where it refers to the device name (line 266):
```cpp
LOG("%s starting...", BRIDGE_NAME);
```

**Step 2: Set wheel circumference from config**

In `setup()`, after the `CscAccumulator g_csc` global (line 42), the value is default-initialized. Add to `setup()` after `pinMode`:
```cpp
g_csc.wheelCircumferenceM = WHEEL_CIRC_MM / 1000.0f;
```

**Step 3: Verify ESP32 build succeeds**

Run: `cd /Users/thomaszola/cycle-bridge && pio run -e esp32dev`
Expected: SUCCESS

**Step 4: Verify native tests still pass**

Run: `cd /Users/thomaszola/cycle-bridge && pio test -e native`
Expected: 17 tests, 0 failures (native env doesn't compile main.cpp)

**Step 5: Commit**

```bash
git add src/main.cpp
git commit -m "refactor: extract hardcoded settings to config.h"
```

---

### Task 4: Add .gitignore

**Files:**
- Create: `.gitignore`

**Step 1: Create .gitignore with PlatformIO defaults**

```
.pio
.vscode
```

**Step 2: Commit**

```bash
git add .gitignore
git commit -m "chore: add .gitignore for PlatformIO"
```

---

### Task 5: Add MIT LICENSE

**Files:**
- Create: `LICENSE`

**Step 1: Create MIT license file**

Use standard MIT license text with:
- Year: 2026
- Author: Thomas Zola

**Step 2: Commit**

```bash
git add LICENSE
git commit -m "chore: add MIT license"
```

---

### Task 6: Write README.md

**Files:**
- Create: `README.md`

**Step 1: Write the README following the approved design outline**

Sections (see design doc for details):
1. Title + one-liner + ASCII diagram (Bike → ESP32 → Watch)
2. What You Need (hardware list, ~$10 total)
3. Quick Start (4-line version: clone, edit config.h, build, flash)
4. Step-by-Step Setup
   - Install PlatformIO (link to docs)
   - Find your bike's MAC address (nRF Connect walkthrough)
   - Edit `config.h` — explain each field
   - Build & flash commands
   - Pair with Apple Watch
5. LED Status Guide (table: off/slow/fast/solid)
6. Troubleshooting (common issues)
7. How It Works (2-3 sentences + link to source)
8. Other FTMS Bikes (tested on SM-420, contributions welcome)
9. License (MIT)

Tone: warm, practical. Semi-technical audience. No BLE theory dumps.

**Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add user guide README for open-source release"
```

---

### Task 7: Final verification

**Step 1: Run native tests**

Run: `cd /Users/thomaszola/cycle-bridge && pio test -e native`
Expected: 17 tests, 0 failures

**Step 2: Run ESP32 build**

Run: `cd /Users/thomaszola/cycle-bridge && pio run -e esp32dev`
Expected: SUCCESS

**Step 3: Review git log**

Run: `git log --oneline -10`
Verify clean commit history.
