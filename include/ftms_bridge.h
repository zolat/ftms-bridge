#ifndef FTMS_BRIDGE_H
#define FTMS_BRIDGE_H

#include <cstdint>
#include <cstddef>
#include <cstring>

struct FtmsIndoorBikeData {
    bool hasSpeed;
    bool hasAvgSpeed;
    bool hasCadence;
    bool hasAvgCadence;
    bool hasTotalDistance;
    bool hasResistance;
    bool hasPower;
    bool hasAvgPower;

    uint16_t instantSpeedRaw;
    uint16_t avgSpeedRaw;
    uint16_t instantCadenceRaw;
    uint16_t avgCadenceRaw;
    uint32_t totalDistance;
    int16_t  resistanceLevel;
    int16_t  instantPower;
    int16_t  avgPower;

    float speedKmh()    const { return instantSpeedRaw / 100.0f; }
    float cadenceRpm()  const { return instantCadenceRaw / 2.0f; }
};

struct CscAccumulator {
    uint32_t cumulativeWheelRevs = 0;
    uint16_t lastWheelEventTime  = 0;
    uint16_t cumulativeCrankRevs = 0;
    uint16_t lastCrankEventTime  = 0;
    float fractionalWheelRevs = 0.0f;
    float fractionalCrankRevs = 0.0f;
    static constexpr float WHEEL_CIRCUMFERENCE_M = 2.096f;
};

inline bool parseFtmsIndoorBikeData(const uint8_t* data, size_t len, FtmsIndoorBikeData& out) {
    if (len < 2) return false;
    memset(&out, 0, sizeof(out));
    uint16_t flags = data[0] | (data[1] << 8);
    size_t pos = 2;

    out.hasSpeed = !(flags & 0x0001);
    if (out.hasSpeed) {
        if (pos + 2 > len) return false;
        out.instantSpeedRaw = data[pos] | (data[pos + 1] << 8);
        pos += 2;
    }
    out.hasAvgSpeed = flags & 0x0002;
    if (out.hasAvgSpeed) {
        if (pos + 2 > len) return false;
        out.avgSpeedRaw = data[pos] | (data[pos + 1] << 8);
        pos += 2;
    }
    out.hasCadence = flags & 0x0004;
    if (out.hasCadence) {
        if (pos + 2 > len) return false;
        out.instantCadenceRaw = data[pos] | (data[pos + 1] << 8);
        pos += 2;
    }
    out.hasAvgCadence = flags & 0x0008;
    if (out.hasAvgCadence) {
        if (pos + 2 > len) return false;
        out.avgCadenceRaw = data[pos] | (data[pos + 1] << 8);
        pos += 2;
    }
    out.hasTotalDistance = flags & 0x0010;
    if (out.hasTotalDistance) {
        if (pos + 3 > len) return false;
        out.totalDistance = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16);
        pos += 3;
    }
    out.hasResistance = flags & 0x0020;
    if (out.hasResistance) {
        if (pos + 2 > len) return false;
        out.resistanceLevel = (int16_t)(data[pos] | (data[pos + 1] << 8));
        pos += 2;
    }
    out.hasPower = flags & 0x0040;
    if (out.hasPower) {
        if (pos + 2 > len) return false;
        out.instantPower = (int16_t)(data[pos] | (data[pos + 1] << 8));
        pos += 2;
    }
    out.hasAvgPower = flags & 0x0080;
    if (out.hasAvgPower) {
        if (pos + 2 > len) return false;
        out.avgPower = (int16_t)(data[pos] | (data[pos + 1] << 8));
        pos += 2;
    }
    return true;
}

inline void updateCsc(CscAccumulator& csc, float speedKmh, float cadenceRpm, uint32_t deltaMs) {
    float deltaSec = deltaMs / 1000.0f;
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
    buf[0] = 0x00;
    buf[1] = 0x00;
    buf[2] = (uint8_t)( powerWatts       & 0xFF);
    buf[3] = (uint8_t)((powerWatts >> 8) & 0xFF);
    return 4;
}

#endif // FTMS_BRIDGE_H
