#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ftms_bridge.h"
#include "config.h"

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
static volatile bool         g_watchConnected = false;
static CscAccumulator        g_csc;

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

// ── BLE server setup ──────────────────────────────────────────────
static void setupBleServer() {
    g_pServer = NimBLEDevice::createServer();
    g_pServer->setCallbacks(new ServerCallbacks());

    // ── CSC Service ───────────────────────────────────────────────
    NimBLEService* pCscService = g_pServer->createService(CSC_SERVICE_UUID);

    g_pCscMeas = pCscService->createCharacteristic(
        CSC_MEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic* pCscFeature = pCscService->createCharacteristic(
        CSC_FEATURE_UUID, NIMBLE_PROPERTY::READ);
    uint16_t cscFeatures = 0x0003;
    pCscFeature->setValue(cscFeatures);

    uint8_t sensorLoc = 0x00;

    NimBLECharacteristic* pCscSensorLoc = pCscService->createCharacteristic(
        SENSOR_LOCATION_UUID, NIMBLE_PROPERTY::READ);
    pCscSensorLoc->setValue(&sensorLoc, 1);

    pCscService->start();

    // ── Cycling Power Service ─────────────────────────────────────
    NimBLEService* pCpService = g_pServer->createService(CP_SERVICE_UUID);

    g_pCpMeas = pCpService->createCharacteristic(
        CP_MEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic* pCpFeature = pCpService->createCharacteristic(
        CP_FEATURE_UUID, NIMBLE_PROPERTY::READ);
    uint32_t cpFeatures = 0x00000000;
    pCpFeature->setValue(cpFeatures);

    NimBLECharacteristic* pCpSensorLoc = pCpService->createCharacteristic(
        SENSOR_LOCATION_UUID, NIMBLE_PROPERTY::READ);
    pCpSensorLoc->setValue(&sensorLoc, 1);

    pCpService->start();

    // ── Advertising ───────────────────────────────────────────────
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(CSC_SERVICE_UUID);
    pAdvertising->addServiceUUID(CP_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    LOG("Server: advertising CSC + CP services");
}

// ── LED status ────────────────────────────────────────────────────
static void updateLed() {
    unsigned long now = millis();
    if (g_ftmsConnected && g_watchConnected) {
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
    g_csc.wheelCircumferenceM = WHEEL_CIRC_MM / 1000.0f;
    LOG("%s starting...", BRIDGE_NAME);

    NimBLEDevice::init(BRIDGE_NAME);
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

    // 1 Hz CSC/CP notifications
    unsigned long now = millis();
    if (now - g_lastNotify >= NOTIFY_INTERVAL_MS) {
        uint32_t deltaMs = now - g_lastNotify;
        g_lastNotify = now;

        if (g_ftmsDataReady) {
            float speedKmh   = g_hasSpeed   ? (g_speedRaw / 100.0f)  : 0.0f;
            float cadenceRpm = g_hasCadence ? (g_cadenceRaw / 2.0f)  : 0.0f;

            updateCsc(g_csc, speedKmh, cadenceRpm, deltaMs);

            if (g_watchConnected) {
                if (g_pCscMeas) {
                    uint8_t buf[16];
                    size_t len = buildCscMeasurement(buf, g_csc, g_hasSpeed, g_hasCadence);
                    g_pCscMeas->setValue(buf, len);
                    g_pCscMeas->notify();
                    LOG("CSC: wheelRevs=%lu crankRevs=%u",
                        g_csc.cumulativeWheelRevs, g_csc.cumulativeCrankRevs);
                }

                if (g_hasPower && g_pCpMeas) {
                    uint8_t cpBuf[8];
                    size_t cpLen = buildCpMeasurement(cpBuf, g_power);
                    g_pCpMeas->setValue(cpBuf, cpLen);
                    g_pCpMeas->notify();
                    LOG("CP: power=%d W", g_power);
                }
            }
        }
    }

    updateLed();
    delay(10);
}
