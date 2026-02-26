# Open-Source Release Restructure Design

**Date:** 2026-02-27
**Status:** Approved

## Goal

Prepare cycle-bridge for public GitHub release. Focus on making it easy for others to configure and use, with a warm practical README as the primary deliverable.

## Audience

Semi-technical cyclists who can follow step-by-step instructions, with quick-start shortcuts for experienced devs.

## Scope

- SM-420 focused, other FTMS bikes welcome (community contributions)
- No logic changes to working firmware
- No new dependencies

## Device Configuration: config.h

User edits a single `config.h` file with 4 settings:

- `BIKE_MAC` — BLE MAC address of their bike
- `BRIDGE_NAME` — device name shown to watch
- `WHEEL_CIRC_MM` — wheel circumference in mm
- `LED_PIN` — GPIO pin for status LED

Rebuild and flash after editing. No runtime config, no filesystem, no WiFi.

## File Structure

```
cycle-bridge/
├── include/
│   ├── config.h           # NEW — user-editable settings
│   └── ftms_bridge.h      # unchanged
├── src/
│   └── main.cpp           # refactored — uses config.h constants
├── test/
│   └── test_native/
│       └── test_main.cpp  # unchanged
├── docs/
│   └── plans/
├── platformio.ini
├── README.md              # NEW — user guide
├── LICENSE                # NEW — MIT
└── .gitignore             # NEW — PlatformIO defaults
```

## Code Changes

- Extract hardcoded MAC, wheel circumference, device name, LED pin from main.cpp into config.h
- Add comment header to main.cpp explaining its role
- No logic changes

## README Structure

1. One-liner + diagram (Bike → ESP32 → Watch)
2. What You Need (hardware list)
3. Quick Start (4-line version for experienced devs)
4. Step-by-Step Setup (PlatformIO install, find MAC, edit config, build, pair)
5. LED Status Guide
6. Troubleshooting
7. How It Works (brief, link to code)
8. Other FTMS Bikes (compatibility notes, contribution invite)
9. License (MIT)

## Out of Scope

- No logic changes to ftms_bridge.h or BLE code
- No new dependencies
- No WiFi/captive portal/runtime config
- No CI/CD or GitHub Actions
- No CONTRIBUTING.md or CODE_OF_CONDUCT
