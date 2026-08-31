#ifndef CONFIG_H
#define CONFIG_H

// ── Your Bike Settings ──────────────────────────────────────────
// Edit these values for your bike, then build & flash.

// BLE MAC address of your FTMS bike.
// Find it with the nRF Connect app: scan for devices advertising "FTMS".
#define BIKE_MAC         "24:00:0c:a0:7c:60"

// Name your watch will see when pairing.
// Keep it to 16 characters or fewer: past that it no longer fits alongside the service
// UUIDs in the advertising packet and gets moved to the scan response, where some
// watches won't show it.
#define BRIDGE_NAME      "SM420 Bridge"

// Wheel circumference in millimeters.
// 2096mm = 700x25c road tire. Adjust for your bike/trainer.
#define WHEEL_CIRC_MM    2096

// GPIO pin for the onboard status LED (2 on most ESP32 dev boards).
#define LED_PIN          2

// GPIO0 = BOOT button on most ESP32 dev boards.
// Short press resets session stats; hold 2s to drop all connected watches.
#define BUTTON_PIN       0

// ── Watch Settings ──────────────────────────────────────────────
// Which sensor profiles to broadcast.
//
// Garmin pairs the bridge as a power meter and reads cadence and speed out of the
// Cycling Power service. Apple Watch reads cadence from Cycling Speed & Cadence.
// Both are on by default so either watch works.
//
// If a watch finds the bridge but refuses to connect, try ENABLE_CSC 0 as a test --
// some watches count one device advertising two profiles against their own sensor
// limit. That build stops the Apple Watch from seeing cadence, so it is a diagnostic,
// not somewhere to leave things.
#define ENABLE_CPS       1
#define ENABLE_CSC       1

// Just Works bonding. Off matches what the Apple Watch has always worked with.
// Try 1 if a watch attempts pairing and then drops the connection.
#define ENABLE_BONDING   0

// How many watches may connect at once. The ESP32 radio allows 3 BLE links in total
// and the bike takes one, so 2 is the ceiling.
#define MAX_CENTRALS     2

// ── Display Settings ───────────────────────────────────────────
// SSD1306 OLED (128x64) on I2C. Set to 0 to disable display.
#define DISPLAY_ENABLED  1
#define DISPLAY_SDA      21
#define DISPLAY_SCL      22
#define DISPLAY_ADDR     0x3C
#define DISPLAY_WIDTH    128
#define DISPLAY_HEIGHT   64

#endif // CONFIG_H
