/**
 * @file main.cpp
 * @verbatim
  ____             ____       _     
 / ___| ___  ___  / ___| __ _| |__  
| |  _ / _ \/ _ \| |  _ / _` | '_ \ 
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/ 
 
 * @endverbatim
 * @brief SimpleLoop — LD2420GeoGab library example (PlatformIO)
 * @author Gabriel Sieben (GeoGab)
 * @version 1.0
 * @date 2026-03
 * @copyright license MIT
 * @details
 * Simple output mode using the poll API — no callbacks required.
 *
 * After calling update(), the last received values are cached and can
 * be read at any time with:
 *   radar.isPresent()       — true if Motion or Presence
 *   radar.getLastStatus()   — None / Motion / Presence
 *   radar.getLastDistance() — distance in cm (0 if unknown)
 *
 * This style is convenient when you want to check the sensor state
 * at a specific point in your loop rather than reacting to events.
 *
 * No sensor configuration is changed — the sensor runs with whatever
 * settings it last saved to its internal flash.
 *
 * Wiring:
 *   ESP32 / ESP32-S3    LD2420
 *   GG_TXPIN        ──  RX
 *   GG_RXPIN        ──  TX
 *   3.3 V           ──  VCC
 *   GND             ──  GND
 */

#include <Arduino.h>
#include "config.h"
#include <LD2420GeoGab.h>

LD2420GeoGab radar;

static const char *statusLabel(LD2420DetectionStatus s) {
    switch (s) {
        case LD2420DetectionStatus::Motion:   return "MOTION";
        case LD2420DetectionStatus::Presence: return "PRESENCE";
        default:                              return "NONE";
    }
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== LD2420GeoGab SimpleLoop ===");

    if (!radar.begin()) {
        Serial.println("[ERROR] Could not communicate with LD2420 — check wiring/baud.");
        while (true) delay(1000);
    }

    Serial.printf("Firmware: %s\n\n", radar.getFirmwareVersion().versionStr.c_str());

    // Explicitly set Simple mode — the sensor retains its last configured
    // mode across power cycles, so we set it here to be safe.
    radar.activateConfigMode();
    radar.setSystemMode(LD2420SystemMode::Simple);
    radar.deactivateConfigMode();

    radar.setUpdateInterval(1000);                // Process at most once every 500 ms — adjust as needed, but note that the sensor outputs at ~10 Hz in Simple mode.

    Serial.println("[OK] Ready — polling every 500 ms.\n");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    // Always call update() to keep the internal ring buffer drained and
    // the cached values fresh. No callbacks are registered — the results
    // are read below via poll getters.
    radar.update();

    // Print the current state every 500 ms.
    if (radar.isPresent()) {
        if (radar.newDataAvailable()) {
            Serial.printf("[%6lu ms]  %-8s  %u cm\n",
                        millis(),
                        statusLabel(radar.getLastStatus()),
                        radar.getLastDistance());
            }
    } else {
        Serial.printf("[%6lu ms]  NONE\n", millis());
    }

    // No delay required once setInterval() is called, but maybe you loop needs a delay for other reasons — adjust as needed.
    delay(10);
}
