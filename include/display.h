#ifndef DISPLAY_H
#define DISPLAY_H

#include "config.h"

#if DISPLAY_ENABLED

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

class BridgeDisplay {
public:
    bool begin() {
        Wire.begin(DISPLAY_SDA, DISPLAY_SCL);
        if (!m_oled.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDR)) {
            return false;
        }
        m_ready = true;
        m_oled.clearDisplay();
        m_oled.setTextColor(SSD1306_WHITE);
        m_oled.display();
        return true;
    }

    bool ready() const { return m_ready; }

    void showStartup(const char* name) {
        if (!m_ready) return;
        m_oled.clearDisplay();
        m_oled.setTextSize(1);
        m_oled.setCursor(0, 0);
        m_oled.print(name);
        m_oled.setCursor(0, 16);
        m_oled.print("Scanning...");
        m_oled.display();
    }

    void update(bool bikeConnected, bool watchConnected,
                float speedKmh, float cadenceRpm, int16_t powerW,
                float distanceKm, unsigned long elapsedMs) {
        if (!m_ready) return;
        m_oled.clearDisplay();

        // ── Status bar (y=0, 8px) ─────────────────────────────
        m_oled.setTextSize(1);
        m_oled.setCursor(0, 0);
        m_oled.print(bikeConnected ? "BIKE" : "bike");
        m_oled.setCursor(32, 0);
        m_oled.print(watchConnected ? "WATCH" : "watch");
        m_oled.setCursor(80, 0);
        if (distanceKm < 10.0f) {
            m_oled.print(distanceKm, 2);
        } else {
            m_oled.print(distanceKm, 1);
        }
        m_oled.print("km");

        // y=9: top divider
        m_oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

        // ── Top-left: /500m split (y=11 label, y=19 value) ────
        m_oled.setTextSize(1);
        m_oled.setCursor(0, 11);
        m_oled.print("/500m");

        m_oled.setTextSize(2);
        m_oled.setCursor(0, 19);
        if (speedKmh > 0.5f) {
            float secPer500m = 1800.0f / speedKmh;
            int mins = (int)(secPer500m / 60.0f);
            float secs = secPer500m - (mins * 60.0f);
            char buf[8];
            snprintf(buf, sizeof(buf), "%d:%04.1f", mins, secs);
            m_oled.print(buf);
        } else {
            m_oled.print("-:--");
        }

        // ── Top-right: cadence (SPM) ──────────────────────────
        m_oled.setTextSize(1);
        m_oled.setCursor(80, 11);
        m_oled.print("SPM");

        m_oled.setTextSize(2);
        m_oled.setCursor(80, 19);
        m_oled.print((int)cadenceRpm);

        // Vertical divider top half
        m_oled.drawLine(72, 9, 72, 35, SSD1306_WHITE);

        // y=36: middle divider
        m_oled.drawLine(0, 36, 127, 36, SSD1306_WHITE);

        // ── Bottom-left: power (y=38 label, y=46 value) ───────
        m_oled.setTextSize(1);
        m_oled.setCursor(0, 38);
        m_oled.print("WATTS");

        m_oled.setTextSize(2);
        m_oled.setCursor(0, 46);
        m_oled.print(powerW);

        // ── Bottom-right: elapsed time ─────────────────────────
        m_oled.setTextSize(1);
        m_oled.setCursor(80, 38);
        m_oled.print("TIME");

        m_oled.setTextSize(2);
        m_oled.setCursor(74, 46);
        unsigned long totalSec = elapsedMs / 1000;
        int mins = totalSec / 60;
        int secs = totalSec % 60;
        char timeBuf[8];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", mins, secs);
        m_oled.print(timeBuf);

        // Vertical divider bottom half
        m_oled.drawLine(72, 36, 72, 63, SSD1306_WHITE);

        m_oled.display();
    }

private:
    Adafruit_SSD1306 m_oled{DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1};
    bool m_ready = false;
};

#endif // DISPLAY_ENABLED
#endif // DISPLAY_H
