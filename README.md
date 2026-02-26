# Cycle Bridge

ESP32 firmware that connects your spin bike to your Apple Watch.

```
Spin Bike ──BLE──> ESP32 ──BLE──> Apple Watch
 (FTMS)            Bridge         Speed + Cadence + Power
```

Most spin bikes broadcast their data over a Bluetooth protocol called FTMS.
Apple Watch doesn't speak FTMS. This tiny bridge sits in the middle, picks up
your bike's speed, cadence, and power, and re-broadcasts it in the format your
Watch already understands.

Plug in the ESP32, start pedaling, and your Watch just sees a bike sensor.


## What You Need

- **Any ESP32 dev board** (~$5-10 on Amazon or AliExpress)
- **Micro USB cable** (make sure it carries data, not just power)
- **A spin bike with FTMS Bluetooth** (tested on the Lifespan SM-420)
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

The easiest path is VS Code with the PlatformIO extension:

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

- `BRIDGE_NAME` -- the name your Watch will see (default: `"SM420 Bridge"`)
- `WHEEL_CIRC_MM` -- wheel circumference in mm (default: `2096`, which is a 700x25c road tire)

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

### e) Pair with Apple Watch

1. Power on the ESP32 and your bike.
2. Wait for the LED to go solid (see below).
3. On your Apple Watch, go to **Settings > Bluetooth** and connect to **SM420 Bridge** (or whatever you set `BRIDGE_NAME` to).
4. Open a cycling app on your Watch -- Apple Workouts, Strava, Wahoo, whatever you like.
5. It should pick up speed, cadence, and power automatically. Ride!


## LED Status

| LED Pattern | Meaning |
|---|---|
| Slow blink (1 Hz) | Scanning for your bike |
| Fast blink (4 Hz) | Connected to bike, waiting for Watch |
| Solid on | Bridge active -- both sides connected, ride! |


## Troubleshooting

**Can't find my bike in nRF Connect**
Make sure the bike is powered on and you're within a few meters. Not all spin bikes
support FTMS -- check your bike's spec sheet for Bluetooth or FTMS support.

**ESP32 connects but no data shows up**
Some bikes require an FTMS Control Point handshake before they start sending data.
This firmware handles that automatically, but if your bike does something unusual it
may not work out of the box.

**Apple Watch doesn't see the bridge**
Check that the ESP32 LED is solid (meaning both connections are active). If it's
still blinking, the bridge hasn't fully connected yet. Try restarting Bluetooth on
the Watch, or power-cycle the ESP32.

**Speed or cadence values seem wrong**
Check `WHEEL_CIRC_MM` in `config.h`. The default (2096 mm) matches a 700x25c road
tire. If your bike or trainer expects a different wheel size, adjust accordingly.

**Upload fails**
Try holding the **BOOT** button on the ESP32 while the upload starts. Also double-check
that your USB cable supports data -- cheap cables often only carry power.


## How It Works

The ESP32 connects to your bike as a BLE client and reads FTMS Indoor Bike Data
notifications. It translates speed and cadence into CSC (Cycling Speed and Cadence)
wheel and crank revolution counts, and forwards power as standard Cycling Power
measurements. Your Watch connects to the ESP32 as if it were an ordinary bike sensor.


## Other FTMS Bikes

This firmware was built for and tested on the **Lifespan SM-420**. It should work
with any bike that broadcasts FTMS Indoor Bike Data, but every bike is a little
different.

If you get it working with another bike, please open an issue or PR -- we'd love to
add it to a compatibility list.


## License

MIT -- see [LICENSE](LICENSE).
