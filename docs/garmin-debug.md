# Debugging a watch that finds the bridge but won't connect

The Garmin Instinct 3 discovers **SM420 Bridge**, you select it, and it refuses with
"too many devices already connected" -- with nothing else connected to the ESP32 and
two connection slots free. This is how to find out why rather than guess.

## Capture a trace

```bash
pio run -e esp32dev-debug -t upload
pio device monitor
```

That build turns on NimBLE's own Bluetooth tracing on top of the bridge's logging.
Then, with the bike on and the ESP32 showing a fast-blinking LED:

1. Delete any existing entry for the bridge on the watch.
2. **Sensors & Accessories > Add New > Search All**, select the bridge.
3. Let it fail.
4. Copy the serial output from the moment `BLE_GAP_EVENT_CONNECT` appears.

## What a healthy connection looks like

```
>> handleGapEvent: BLE_GAP_EVENT_CONNECT
Server: watch connected  peer=xx:xx:xx:xx:xx:xx  1/2
>> handleGapEvent: BLE_GAP_EVENT_MTU
Server: MTU=247  peer=xx:xx:xx:xx:xx:xx
>> handleGapEvent: BLE_GAP_EVENT_SUBSCRIBE
Server: 0x2a63 subscribed  peer=xx:xx:xx:xx:xx:xx
CP: flags=0x0030 pwr=180W crankRevs=42 wheelRevs=311
```

The watch connects, negotiates an MTU, discovers services, enables notifications on
the Cycling Power Measurement (0x2a63), and data starts flowing.

## Reading a failed trace

Find the last thing that happened before `BLE_GAP_EVENT_DISCONNECT`.

| Last event before disconnect | What it means | What to try |
|---|---|---|
| `CONNECT`, then nothing, then `DISCONNECT` | The watch connected and dropped before doing anything. Not a GATT problem -- it decided against the connection itself. | Update the watch firmware. Instinct 3 shipped a Bluetooth sensor connectivity fix in 9.25. |
| `CONNECT`, `MTU`, then `DISCONNECT` with no `SUBSCRIBE` | It got as far as reading the GATT table and didn't like what it found. | This is the case the new Device Information, Battery and Control Point services address. If it still fails here, try `ENABLE_CSC 0` -- some watches count one device advertising two sensor profiles against their own limit. |
| `ENC_CHANGE`, `PASSKEY_ACTION` or `REPEAT_PAIRING` appears | The watch is trying to pair, and being refused. | Set `ENABLE_BONDING 1` in `config.h` and reflash. Also check for a `Server: pairing complete` line -- `encrypted=0` means it failed. |
| `SUBSCRIBE` happens, then it disconnects | It got everything it needed and left anyway. | Look at what the bridge sent next -- check the `CP: flags=...` line looks sane. |
| No `CONNECT` at all | The watch never reached us; the error is entirely watch-side. | Check the bridge is advertising: `w:0` on the OLED with a fast-blinking LED. Hold BOOT for 2 seconds to force a restart of advertising. |

## Config toggles

All in `include/config.h`; reflash after changing.

| Setting | Default | Try when |
|---|---|---|
| `ENABLE_CSC` | `1` | Set `0` if the trace stops after MTU. **Diagnostic only** -- it stops the Apple Watch seeing cadence. |
| `ENABLE_CPS` | `1` | Set `0` only to confirm which profile a watch is latching onto. |
| `ENABLE_BONDING` | `0` | Set `1` if the trace shows pairing attempts. |
| `MAX_CENTRALS` | `2` | Leave alone. The radio allows three links and the bike holds one. |

## Sanity check before blaming the watch

Point nRF Connect at the bridge from a phone first. You should see:

- **Cycling Power (0x1818)** -- Measurement `0x2A63` notifying 14-byte packets starting
  `30 00`, Feature `0x2A65` reading `0C 00 00 00`, Sensor Location, Control Point.
- **Cycling Speed and Cadence (0x1816)**, **Device Information (0x180A)**,
  **Battery (0x180F)**.

The 14-byte Cycling Power notification lays out as (0-indexed):

| Bytes | Field |
|---|---|
| 0-1 | Flags -- `30 00` means wheel + crank data present |
| 2-3 | Instantaneous power, watts |
| 4-7 | Cumulative wheel revolutions |
| 8-9 | Last wheel event time, 1/2048 s |
| 10-11 | Cumulative crank revolutions |
| 12-13 | Last crank event time, 1/1024 s |

Subscribe to `0x2A63` and pedal: bytes 10-11 should climb. If that works and a watch
still won't connect, the bridge is doing its job and the problem is in how that watch
handles it.
