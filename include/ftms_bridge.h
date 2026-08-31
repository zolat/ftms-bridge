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
    float wheelCircumferenceM = 2.096f;
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
        csc.fractionalWheelRevs += distanceM / csc.wheelCircumferenceM;
        float wheelRevsPerSec = speedMs / csc.wheelCircumferenceM;
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

// ── Cycling Power Service (0x1818) ────────────────────────────────
// Cycling Power Feature (0x2A65) bits, per the Bluetooth SIG Cycling Power Service.
static const uint32_t CP_FEATURE_WHEEL_REV = 0x00000004;  // bit 2
static const uint32_t CP_FEATURE_CRANK_REV = 0x00000008;  // bit 3

// Cycling Power Measurement (0x2A63) flag bits.
static const uint16_t CP_FLAG_WHEEL_REV = 0x0010;  // bit 4
static const uint16_t CP_FLAG_CRANK_REV = 0x0020;  // bit 5

// Cycling Power Feature goes on the wire as a little-endian uint32. Writing it
// big-endian sets bit 27 instead of bit 3 and the watch silently drops cadence.
inline size_t serializeCpFeature(uint8_t* buf, uint32_t features) {
    buf[0] =  features        & 0xFF;
    buf[1] = (features >>  8) & 0xFF;
    buf[2] = (features >> 16) & 0xFF;
    buf[3] = (features >> 24) & 0xFF;
    return 4;
}

// Cycling Power Measurement. Field order is fixed by the spec: flags, instantaneous
// power, wheel data, crank data.
//
// A Garmin (or any head unit) that pairs this as a power meter reads cadence from the
// crank fields here, not from CSC -- which is why they have to be present.
//
// Watch the units: the CPS wheel event time is 1/2048 s, while the crank event time
// here and everything in CSC is 1/1024 s. Doubling the accumulator's 1/1024 s value is
// correct, including across the uint16 wrap.
inline size_t buildCpMeasurement(uint8_t* buf, int16_t powerWatts,
                                 const CscAccumulator& csc,
                                 bool includeWheel, bool includeCrank) {
    uint16_t flags = 0;
    if (includeWheel) flags |= CP_FLAG_WHEEL_REV;
    if (includeCrank) flags |= CP_FLAG_CRANK_REV;

    size_t pos = 0;
    buf[pos++] =  flags       & 0xFF;
    buf[pos++] = (flags >> 8) & 0xFF;

    uint16_t power = (uint16_t)powerWatts;
    buf[pos++] =  power       & 0xFF;
    buf[pos++] = (power >> 8) & 0xFF;

    if (includeWheel) {
        buf[pos++] =  csc.cumulativeWheelRevs        & 0xFF;
        buf[pos++] = (csc.cumulativeWheelRevs >> 8)  & 0xFF;
        buf[pos++] = (csc.cumulativeWheelRevs >> 16) & 0xFF;
        buf[pos++] = (csc.cumulativeWheelRevs >> 24) & 0xFF;
        uint16_t wheelTime2048 = (uint16_t)(csc.lastWheelEventTime * 2);
        buf[pos++] =  wheelTime2048       & 0xFF;
        buf[pos++] = (wheelTime2048 >> 8) & 0xFF;
    }
    if (includeCrank) {
        buf[pos++] =  csc.cumulativeCrankRevs        & 0xFF;
        buf[pos++] = (csc.cumulativeCrankRevs >> 8)  & 0xFF;
        buf[pos++] =  csc.lastCrankEventTime         & 0xFF;
        buf[pos++] = (csc.lastCrankEventTime >> 8)   & 0xFF;
    }
    return pos;
}

#endif // FTMS_BRIDGE_H
