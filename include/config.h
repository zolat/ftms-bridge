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
