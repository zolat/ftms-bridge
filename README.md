# FTMS Bike Bridge

ESP32 firmware that connects your spin bike to your watch.

```
Spin Bike ──BLE──> ESP32 ──BLE──> Apple Watch or Garmin
 (FTMS)            Bridge         Speed + Cadence + Power
```

Most spin bikes broadcast data over a Bluetooth protocol called FTMS.
Watches don't work with FTMS. This bridge sits in the middle and
re-broadcasts speed, cadence, and power in a format they can use.

The watch just sees a regular bike sensor.


## What You Need

- **Any ESP32 dev board** (~$5-10 on Amazon or AliExpress)
- **Micro USB cable** (make sure it carries data, not just power)
- **A spin bike with FTMS Bluetooth** (tested on the Lifespan SM-420)
- **An Apple Watch or a Garmin** (tested on Apple Watch and Garmin Epix Gen 2)
- **nRF Connect app** on your phone (free on iOS and Android) -- you'll use this once to find your bike's Bluetooth address


## Quick Start

If you already have PlatformIO installed and know what you're doing:

```bash
git clone https://github.com/zolat/ftms-bridge.git
# Edit include/config.h — set BIKE_MAC to your bike's address
pio run -e esp32dev -t upload
```


## Step-by-Step Setup

### a) Install PlatformIO

The simplest option is VS Code with the PlatformIO extension:

https://platformio.org/install/ide?install=vscode

Or, if you prefer the command line:

```bash
pip install platformio
```

### b) Find Your Bike's MAC Address

1. Install **nRF Connect** on your phone (free on the App Store / Google Play).
2. Turn on your spin bike.
3. Open nRF Connect and tap **Scan**.
4. Look for a device advertising **Fitness Machine** or **FTMS**.
5. Note the MAC address -- it looks like `XX:XX:XX:XX:XX:XX`.

If you see multiple FTMS devices, try turning your bike off and scanning again to
see which one disappears.

### c) Configure

Open `include/config.h` and set your bike's MAC address:

```c
#define BIKE_MAC    "XX:XX:XX:XX:XX:XX"   // your bike's address
```

You can also change:

- `BRIDGE_NAME` -- the name your watch will see (default: `"SM420 Bridge"`)
- `WHEEL_CIRC_MM` -- wheel circumference in mm (default: `2096`, which is a 700x25c road tire)
- `ENABLE_CPS` / `ENABLE_CSC` -- which sensor profiles to broadcast (both on by default)

### d) Build & Flash

1. Connect the ESP32 to your computer with a USB cable.
2. Run:

```bash
pio run -e esp32dev -t upload
```

If the upload fails, you may need to specify the serial port:

```bash
# macOS
pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-XXXX

# Windows
pio run -e esp32dev -t upload --upload-port COM3
```

### e) Pair with Your Watch

Power on the ESP32 and your bike first, and wait for the LED to stop blinking slowly
(see below). Then:

**Apple Watch**

1. Go to **Settings > Bluetooth** and connect to **SM420 Bridge** (or whatever you set
   `BRIDGE_NAME` to).
2. Open a cycling app -- Apple Workouts, Strava, Wahoo, whatever you like.
3. It should pick up speed, cadence, and power automatically.

**Garmin**

1. Go to **Settings > Sensors & Accessories > Add New > Search All**.
2. Pick **SM420 Bridge**.
3. Garmin pairs the bridge as a **power meter**, and cadence and speed come through it.
   There is no separate cadence sensor to add -- that is how real power meters work too.
4. Set the sensor's **wheel size** on the watch to match `WHEEL_CIRC_MM` (2096 mm by
   default). Indoors there is no GPS to calibrate against, so a mismatch here shows up
   as wrong speed.

Two watches can be connected at once. The ESP32's radio allows three Bluetooth links in
total and the bike takes one, so that is the ceiling.


## Status Lights

| LED Pattern | Meaning |
|---|---|
| Slow blink (1 Hz) | Scanning for your bike |
| Fast blink (4 Hz) | Connected to bike, waiting for a watch |
| Solid on | Bridge active -- both sides connected |

If you have the OLED fitted, the top-left of the status bar shows `BIKE`/`bike` for the
bike link and `W:1` for how many watches are connected -- handy for spotting a watch
that has silently reconnected in your pocket.


## Buttons

| Action | Effect |
|---|---|
| Short press BOOT | Reset session distance and timer |
| Hold BOOT 2 seconds | Disconnect every watch and resume advertising |


## Troubleshooting

**Can't find my bike in nRF Connect**
Make sure the bike is powered on and you're within a few meters. Not all spin bikes
support FTMS -- check your bike's spec sheet for Bluetooth or FTMS support.

**ESP32 connects but no data shows up**
Some bikes require an FTMS Control Point handshake before they start sending data.
This firmware handles that automatically, but if your bike does something unusual it
may not work out of the box.

**My watch doesn't see the bridge**
Check the ESP32 LED. Slow blink means it is still looking for your bike. If nothing
shows up at all, hold **BOOT** for 2 seconds -- that drops any watch that reconnected
silently and restarts advertising.

**Garmin shows power but no cadence**
Make sure you are on this version of the firmware. Older builds sent power on its own,
with no crank data, and Garmin reads a power meter's cadence out of those crank fields.
Delete the sensor on the watch and pair it again after flashing.

**Garmin connects then immediately disconnects, or reports "too many devices already connected"**
The watch is asking to bond and being refused. Check `ENABLE_BONDING` is `1` in
`config.h` -- it is by default, because the Instinct 3 requires it. Delete the sensor
on the watch and pair again after reflashing; Garmin holds on to half-finished pairing
records and a stale one will fail for its own reasons.

If that isn't it, capture what actually happens rather than guessing -- see
[docs/garmin-debug.md](docs/garmin-debug.md):

```bash
pio run -e esp32dev-debug -t upload
pio device monitor
```

**Only two watches can be connected at once**
The ESP32 radio allows three Bluetooth links and the bike holds one. Garmin watches
reconnect to saved sensors on their own, so a watch in the next room may be using a
slot. The OLED shows `W:1` / `W:2`; hold **BOOT** for 2 seconds to drop them all.

## How It Works

The ESP32 connects to your bike as a BLE client and reads FTMS Indoor Bike Data
notifications. It turns speed and cadence into wheel and crank revolution counts and
broadcasts them two ways:

- **Cycling Power (0x1818)** -- power, plus crank revolutions for cadence and wheel
  revolutions for speed. This is what Garmin pairs with, and carries everything on its
  own.
- **Cycling Speed and Cadence (0x1816)** -- wheel and crank revolutions. This is what
  the Apple Watch reads cadence from.

Your watch connects to the ESP32 as if it were an ordinary bike sensor.

Running out of connections? The classic ESP32's Bluetooth controller is fixed at three
links. An ESP32-C3 or S3 allows six if you need more headroom.


## Other FTMS Bikes

This firmware was built for and tested on the **Lifespan SM-420**. It should work
with any bike that broadcasts FTMS Indoor Bike Data, but every bike is a little
different.

If you get it working with another bike, open an issue or PR and we'll add it to
a compatibility list.


## License

MIT -- see [LICENSE](LICENSE).
