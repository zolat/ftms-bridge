#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ftms_bridge.h"
#include "config.h"
#include "display.h"

#if DEBUG_LOG
  #define LOG(fmt, ...) Serial.printf(fmt "\n", ##__VA_ARGS__)
#else
  #define LOG(fmt, ...)
#endif

// ── UUIDs ─────────────────────────────────────────────────────────
static const NimBLEUUID FTMS_SERVICE_UUID((uint16_t)0x1826);
static const NimBLEUUID FTMS_INDOOR_BIKE_UUID((uint16_t)0x2AD2);
static const NimBLEUUID CSC_SERVICE_UUID((uint16_t)0x1816);
static const NimBLEUUID CSC_MEASUREMENT_UUID((uint16_t)0x2A5B);
static const NimBLEUUID CSC_FEATURE_UUID((uint16_t)0x2A5C);
static const NimBLEUUID SENSOR_LOCATION_UUID((uint16_t)0x2A5D);
static const NimBLEUUID CP_SERVICE_UUID((uint16_t)0x1818);
static const NimBLEUUID CP_MEASUREMENT_UUID((uint16_t)0x2A63);
static const NimBLEUUID CP_FEATURE_UUID((uint16_t)0x2A65);
static const NimBLEUUID CP_CONTROL_POINT_UUID((uint16_t)0x2A66);
static const NimBLEUUID DIS_SERVICE_UUID((uint16_t)0x180A);
static const NimBLEUUID DIS_MANUFACTURER_UUID((uint16_t)0x2A29);
static const NimBLEUUID DIS_MODEL_UUID((uint16_t)0x2A24);
static const NimBLEUUID DIS_FIRMWARE_UUID((uint16_t)0x2A26);
static const NimBLEUUID BATTERY_SERVICE_UUID((uint16_t)0x180F);
static const NimBLEUUID BATTERY_LEVEL_UUID((uint16_t)0x2A19);

// Appearance: Cycling: Power Sensor. Watches use it as a hint about what we are.
static const uint16_t APPEARANCE_CYCLING_POWER_SENSOR = 0x0484;

static const char* FIRMWARE_REVISION = "1.1.0";

// ── FTMS client state (written by BLE callback, read by loop) ────
static volatile bool     g_ftmsConnected  = false;
static volatile bool     g_ftmsDataReady  = false;
static volatile uint16_t g_speedRaw       = 0;
static volatile uint16_t g_cadenceRaw     = 0;
static volatile int16_t  g_power          = 0;
static volatile bool     g_hasSpeed       = false;
static volatile bool     g_hasCadence     = false;
static volatile bool     g_hasPower       = false;

// ── BLE client objects ────────────────────────────────────────────
static NimBLEAdvertisedDevice* g_targetDevice = nullptr;
static bool g_doConnect = false;
static NimBLEClient* g_pClient = nullptr;

// ── BLE server objects ────────────────────────────────────────────
static NimBLEServer*         g_pServer       = nullptr;
static NimBLECharacteristic* g_pCscMeas      = nullptr;
static NimBLECharacteristic* g_pCpMeas       = nullptr;
static CscAccumulator        g_csc;

// How many watches are connected right now. The bike is a client link, so it is not
// counted here.
static uint8_t connectedCentrals() {
    return g_pServer ? (uint8_t)g_pServer->getConnectedCount() : 0;
}

// ── Display ──────────────────────────────────────────────────────
#if DISPLAY_ENABLED
static BridgeDisplay g_display;
#endif

// ── Session tracking ─────────────────────────────────────────────
static float          g_distanceKm   = 0.0f;
static unsigned long  g_sessionStart = 0;
static bool           g_sessionActive = false;

// ── Button (short press: reset stats, long press: drop watches) ──
static unsigned long g_buttonDownAt   = 0;
static bool          g_buttonWasDown  = false;
static bool          g_longPressFired = false;
static const unsigned long DEBOUNCE_MS   = 50;
static const unsigned long LONG_PRESS_MS = 2000;

static void resetSession() {
    g_distanceKm = 0.0f;
    g_sessionStart = 0;
    g_sessionActive = false;
    g_csc = CscAccumulator();
    g_csc.wheelCircumferenceM = WHEEL_CIRC_MM / 1000.0f;
    LOG("Session reset");
}

// ── Timing ────────────────────────────────────────────────────────
static unsigned long g_lastNotify = 0;
static const unsigned long NOTIFY_INTERVAL_MS = 1000;

// ── FTMS notification callback ────────────────────────────────────
static void ftmsNotifyCallback(NimBLERemoteCharacteristic* pChar,
                                uint8_t* pData, size_t length, bool isNotify) {
    FtmsIndoorBikeData bikeData;
    if (!parseFtmsIndoorBikeData(pData, length, bikeData)) {
        LOG("FTMS: parse failed (%d bytes)", length);
        return;
    }

    // Only update fields that are present — bike sends split notifications
    if (bikeData.hasSpeed) {
        g_hasSpeed = true;
        g_speedRaw = bikeData.instantSpeedRaw;
    }
    if (bikeData.hasCadence) {
        g_hasCadence = true;
        g_cadenceRaw = bikeData.instantCadenceRaw;
    }
    if (bikeData.hasPower) {
        g_hasPower = true;
        g_power = bikeData.instantPower;
    }
    g_ftmsDataReady = true;

    LOG("FTMS: spd=%.1f km/h  cad=%.0f rpm  pwr=%d W",
        bikeData.hasSpeed   ? bikeData.speedKmh()   : 0.0f,
        bikeData.hasCadence ? bikeData.cadenceRpm()  : 0.0f,
        bikeData.hasPower   ? bikeData.instantPower  : 0);
}

// ── Target device (hardcoded SM-420) ──────────────────────────────
static const NimBLEAddress TARGET_ADDRESS(BIKE_MAC);

// ── Scan callbacks ────────────────────────────────────────────────
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* device) override {
        if (device->getAddress() == TARGET_ADDRESS) {
            LOG("Scan: found SM-420! RSSI=%d", device->getRSSI());
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
        LOG("Client: disconnected from SM-420 — will reconnect");
        g_ftmsConnected = false;
        g_ftmsDataReady = false;
        g_doConnect = true;
    }
};

// ── Server callbacks ──────────────────────────────────────────────
// These log more than they strictly need to. A watch that finds the bridge but
// refuses to connect leaves a trail here -- how far it got, whether it negotiated an
// MTU, whether it tried to pair -- which is the difference between diagnosing the
// problem and guessing at it.
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
        LOG("Server: watch connected  peer=%s  %u/%u",
            NimBLEAddress(desc->peer_ota_addr).toString().c_str(),
            (unsigned)pServer->getConnectedCount(), (unsigned)MAX_CENTRALS);

        // NimBLE stops advertising once a central connects and does not resume on its
        // own. Without this the bridge only ever accepts one watch per boot.
        if (pServer->getConnectedCount() < MAX_CENTRALS) {
            NimBLEDevice::startAdvertising();
        }
    }
    void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
        LOG("Server: watch disconnected  peer=%s  %u/%u",
            NimBLEAddress(desc->peer_ota_addr).toString().c_str(),
            (unsigned)pServer->getConnectedCount(), (unsigned)MAX_CENTRALS);
    }
    void onMTUChange(uint16_t MTU, ble_gap_conn_desc* desc) override {
        LOG("Server: MTU=%u  peer=%s", MTU,
            NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    }
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        LOG("Server: pairing complete  encrypted=%d authenticated=%d bonded=%d",
            desc->sec_state.encrypted, desc->sec_state.authenticated,
            desc->sec_state.bonded);
    }
};

// ── Characteristic callbacks ──────────────────────────────────────
// Whether a watch reached the point of enabling notifications, or gave up earlier.
class NotifyCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* pChar, ble_gap_conn_desc* desc,
                     uint16_t subValue) override {
        LOG("Server: %s %s  peer=%s",
            pChar->getUUID().toString().c_str(),
            subValue == 0 ? "unsubscribed" : "subscribed",
            NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    }
};

// Every real power meter exposes a control point. We support no opcodes, but we answer
// properly rather than leaving a head unit waiting on service discovery.
class CpControlPointCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string req = pChar->getValue();
        uint8_t opcode  = req.empty() ? 0 : (uint8_t)req[0];
        // Response Code, request opcode, Op Code Not Supported.
        uint8_t rsp[3] = {0x20, opcode, 0x02};
        pChar->indicate(rsp, sizeof(rsp));
        LOG("CP Control Point: opcode 0x%02X -> not supported", opcode);
    }
};

// ── Recovery helpers ──────────────────────────────────────────────
static void dropAllCentrals() {
    if (!g_pServer) return;
    std::vector<uint16_t> peers = g_pServer->getPeerDevices();
    LOG("Button: dropping %u connected watch(es)", (unsigned)peers.size());
    for (size_t i = 0; i < peers.size(); i++) {
        g_pServer->disconnect(peers[i]);
    }
    NimBLEDevice::startAdvertising();
}

// Belt and braces: if a slot is free but the radio is not advertising, a watch has no
// way back in. Cheap to check, and it covers whatever stopped it.
static void ensureAdvertising() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    if (now - lastCheck < 5000) return;
    lastCheck = now;

    if (connectedCentrals() >= MAX_CENTRALS) return;
    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    if (!pAdv->isAdvertising()) {
        LOG("Advertising had stopped with %u/%u watches -- restarting",
            (unsigned)connectedCentrals(), (unsigned)MAX_CENTRALS);
        pAdv->start();
    }
}

static void handleButton() {
    bool down = (digitalRead(BUTTON_PIN) == LOW);
    unsigned long now = millis();

    if (down && !g_buttonWasDown) {
        g_buttonDownAt   = now;
        g_longPressFired = false;
    } else if (down && !g_longPressFired && (now - g_buttonDownAt) >= LONG_PRESS_MS) {
        g_longPressFired = true;
        dropAllCentrals();
    } else if (!down && g_buttonWasDown) {
        if (!g_longPressFired && (now - g_buttonDownAt) >= DEBOUNCE_MS) {
            resetSession();
        }
    }
    g_buttonWasDown = down;
}

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

    // Try acknowledged subscribe (some devices require Write Request for CCCD)
    if (!pChar->subscribe(true, ftmsNotifyCallback, true)) {
        LOG("Acknowledged subscribe failed, trying unacknowledged...");
        if (!pChar->subscribe(true, ftmsNotifyCallback, false)) {
            LOG("Failed to subscribe to notifications!");
            g_pClient->disconnect();
            return false;
        }
    }
    LOG("Subscribed to FTMS Indoor Bike Data");

    // Write to FTMS Control Point to start data flow
    NimBLERemoteCharacteristic* pCtrl = pService->getCharacteristic(
        NimBLEUUID((uint16_t)0x2AD9));
    if (pCtrl) {
        // Request Control (opcode 0x00)
        uint8_t reqCtrl[] = {0x00};
        if (pCtrl->writeValue(reqCtrl, 1, true)) {
            LOG("FTMS Control: requested control");
            // Start or Resume (opcode 0x07)
            uint8_t startCmd[] = {0x07};
            if (pCtrl->writeValue(startCmd, 1, true)) {
                LOG("FTMS Control: sent Start command");
            } else {
                LOG("FTMS Control: Start command failed");
            }
        } else {
            LOG("FTMS Control: request control failed");
        }
    } else {
        LOG("FTMS Control Point not found (OK, may not be required)");
    }

    return true;
}

// ── Scan for SM-420 ───────────────────────────────────────────────
static void startScan() {
    LOG("Scanning for SM-420 (%s)...", TARGET_ADDRESS.toString().c_str());
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), false);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->start(10, false);
}

// ── BLE server setup ────────────────────────────────────────

// Real sensors publish who they are and how much battery they have left. Some watches
// will discover a device without them and then refuse to use it, so we provide both.
static void setupDeviceInfoService() {
    NimBLEService* pDis = g_pServer->createService(DIS_SERVICE_UUID);
    pDis->createCharacteristic(DIS_MANUFACTURER_UUID, NIMBLE_PROPERTY::READ)
        ->setValue(std::string("ftms-bridge"));
    pDis->createCharacteristic(DIS_MODEL_UUID, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(BRIDGE_NAME));
    pDis->createCharacteristic(DIS_FIRMWARE_UUID, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(FIRMWARE_REVISION));
    pDis->start();
}

static void setupBatteryService() {
    NimBLEService* pBat = g_pServer->createService(BATTERY_SERVICE_UUID);
    NimBLECharacteristic* pLevel = pBat->createCharacteristic(
        BATTERY_LEVEL_UUID, NIMBLE_PROPERTY::READ);
    uint8_t level = 100;  // USB powered — always full
    pLevel->setValue(&level, 1);
    pBat->start();
}

static void setupBleServer() {
    g_pServer = NimBLEDevice::createServer();
    g_pServer->setCallbacks(new ServerCallbacks());
    g_pServer->advertiseOnDisconnect(true);

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

#if ENABLE_CSC
    // ── CSC Service ─────────────────────────────────────
    NimBLEService* pCscService = g_pServer->createService(CSC_SERVICE_UUID);

    g_pCscMeas = pCscService->createCharacteristic(
        CSC_MEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);
    g_pCscMeas->setCallbacks(new NotifyCallbacks());

    NimBLECharacteristic* pCscFeature = pCscService->createCharacteristic(
        CSC_FEATURE_UUID, NIMBLE_PROPERTY::READ);
    uint16_t cscFeatures = 0x0003;
    pCscFeature->setValue(cscFeatures);

    NimBLECharacteristic* pCscSensorLoc = pCscService->createCharacteristic(
        SENSOR_LOCATION_UUID, NIMBLE_PROPERTY::READ);
    uint8_t cscLoc = 0x00;  // Other
    pCscSensorLoc->setValue(&cscLoc, 1);

    pCscService->start();
    pAdvertising->addServiceUUID(CSC_SERVICE_UUID);
#endif

#if ENABLE_CPS
    // ── Cycling Power Service ─────────────────────────────
    NimBLEService* pCpService = g_pServer->createService(CP_SERVICE_UUID);

    g_pCpMeas = pCpService->createCharacteristic(
        CP_MEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);
    g_pCpMeas->setCallbacks(new NotifyCallbacks());

    // Declare wheel + crank support. A watch paired with this as a power meter reads
    // cadence out of the crank fields, and checks these bits before believing them.
    NimBLECharacteristic* pCpFeature = pCpService->createCharacteristic(
        CP_FEATURE_UUID, NIMBLE_PROPERTY::READ);
    uint8_t featureBuf[4];
    size_t featureLen = serializeCpFeature(
        featureBuf, CP_FEATURE_WHEEL_REV | CP_FEATURE_CRANK_REV);
    pCpFeature->setValue(featureBuf, featureLen);

    NimBLECharacteristic* pCpSensorLoc = pCpService->createCharacteristic(
        SENSOR_LOCATION_UUID, NIMBLE_PROPERTY::READ);
    uint8_t cpLoc = 0x05;  // Left Crank — the cadence really is crank-derived
    pCpSensorLoc->setValue(&cpLoc, 1);

    NimBLECharacteristic* pCpControl = pCpService->createCharacteristic(
        CP_CONTROL_POINT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
    pCpControl->setCallbacks(new CpControlPointCallbacks());

    pCpService->start();
    pAdvertising->addServiceUUID(CP_SERVICE_UUID);
#endif

    setupDeviceInfoService();
    setupBatteryService();

    // ── Advertising ─────────────────────────────────────
    // Service UUIDs stay in the advertising payload rather than the scan response --
    // that is where watches look when deciding what kind of sensor this is.
    pAdvertising->setAppearance(APPEARANCE_CYCLING_POWER_SENSOR);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    LOG("Server: advertising as \"%s\" (CPS=%d CSC=%d, up to %u watches)",
        BRIDGE_NAME, ENABLE_CPS, ENABLE_CSC, (unsigned)MAX_CENTRALS);
}

// ── LED status ────────────────────────────────────────────────────
static void updateLed() {
    unsigned long now = millis();
    if (g_ftmsConnected && connectedCentrals() > 0) {
        digitalWrite(LED_PIN, HIGH);
    } else if (g_ftmsConnected) {
        digitalWrite(LED_PIN, (now / 125) % 2);
    } else {
        digitalWrite(LED_PIN, (now / 500) % 2);
    }
}

// ── Arduino entry points ──────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    g_csc.wheelCircumferenceM = WHEEL_CIRC_MM / 1000.0f;
    LOG("%s starting...", BRIDGE_NAME);

    #if DISPLAY_ENABLED
    if (g_display.begin()) {
        LOG("Display: initialized");
        g_display.showStartup(BRIDGE_NAME);
    } else {
        LOG("Display: init failed");
    }
    #endif

    NimBLEDevice::init(BRIDGE_NAME);
    // No passkey, and bonding only if the config asks for it. Cycling sensors are
    // allowed to run unencrypted, and that is what the Apple Watch has always used.
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(ENABLE_BONDING, false, true);
    setupBleServer();
    startScan();
}

void loop() {
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

    handleButton();
    ensureAdvertising();

    // 1 Hz CSC/CP notifications
    unsigned long now = millis();
    if (now - g_lastNotify >= NOTIFY_INTERVAL_MS) {
        uint32_t deltaMs = now - g_lastNotify;
        g_lastNotify = now;

        if (g_ftmsDataReady) {
            float speedKmh   = g_hasSpeed   ? (g_speedRaw / 100.0f)  : 0.0f;
            float cadenceRpm = g_hasCadence ? (g_cadenceRaw / 2.0f)  : 0.0f;

            // Start session clock on first real data
            if (!g_sessionActive && speedKmh > 0.5f) {
                g_sessionActive = true;
                g_sessionStart = now;
            }

            // Accumulate distance
            if (speedKmh > 0.0f) {
                float distKm = (speedKmh / 3600.0f) * (deltaMs / 1000.0f);
                g_distanceKm += distKm;
            }

            updateCsc(g_csc, speedKmh, cadenceRpm, deltaMs);

            unsigned long elapsed = g_sessionActive ? (now - g_sessionStart) : 0;
            uint8_t centrals = connectedCentrals();

            #if DISPLAY_ENABLED
            g_display.update(g_ftmsConnected, centrals,
                             speedKmh, cadenceRpm, g_power,
                             g_distanceKm, elapsed);
            #endif

            if (centrals > 0) {
                if (g_pCscMeas) {
                    uint8_t buf[16];
                    size_t len = buildCscMeasurement(buf, g_csc, g_hasSpeed, g_hasCadence);
                    g_pCscMeas->setValue(buf, len);
                    g_pCscMeas->notify();
                    LOG("CSC: wheelRevs=%lu crankRevs=%u",
                        g_csc.cumulativeWheelRevs, g_csc.cumulativeCrankRevs);
                }

                if (g_pCpMeas) {
                    // Always send, even with no power reading. A watch paired with
                    // this as a power meter takes its cadence from here, so the
                    // stream has to keep running.
                    int16_t power = g_hasPower ? g_power : 0;
                    uint8_t cpBuf[16];
                    size_t cpLen = buildCpMeasurement(cpBuf, power, g_csc,
                                                      g_hasSpeed, g_hasCadence);
                    g_pCpMeas->setValue(cpBuf, cpLen);
                    g_pCpMeas->notify();
                    LOG("CP: flags=0x%04X pwr=%dW crankRevs=%u wheelRevs=%lu",
                        (unsigned)(cpBuf[0] | (cpBuf[1] << 8)), power,
                        g_csc.cumulativeCrankRevs,
                        (unsigned long)g_csc.cumulativeWheelRevs);
                }
            }
        }
    }

    updateLed();
    delay(10);
}
