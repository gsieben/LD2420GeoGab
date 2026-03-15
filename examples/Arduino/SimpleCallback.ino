/**
 * @file SimpleCallback.ino
 * @verbatim
  ____             ____       _     
 / ___| ___  ___  / ___| __ _| |__  
| |  _ / _ \/ _ \| |  _ / _` | '_ \ 
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/ 
 
 * @endverbatim
 * @brief SimpleCallback — LD2420GeoGab library example (Arduino)
 * @author Gabriel Sieben (GeoGab)
 * @version 1.0
 * @date 2024-06-01
 * @copyright license MIT
 * @details
 * Minimal getting-started sketch using Simple output mode and callbacks.
 *
 * The sensor outputs ASCII text lines:
 *   "ON\r\n"         → presence detected (still or moving person)
 *   "OFF\r\n"        → no target
 *   "Range XXXX\r\n" → motion detected, XXXX = distance in cm
 *
 * This example registers two callbacks:
 *   - setPresenceCallback() — fires only on state transitions (present ↔ gone)
 *   - setDistanceCallback() — fires every frame that contains a distance value
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
 *
 * Pin and baud defaults are set in platformio.ini (build_flags).
 */

#include <Arduino.h>
#include "config.h"
#include <LD2420GeoGab.h>

LD2420GeoGab radar;

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== LD2420GeoGab SimpleCallback ===");

    if (!radar.begin()) {
        Serial.println("[ERROR] Could not communicate with LD2420 — check wiring/baud.");
        while (true) delay(1000);
    }

    Serial.printf("Firmware: %s\n\n", radar.getFirmwareVersion().versionStr.c_str());

    // Set Simple output mode — the sensor sends short ASCII text lines.
    // Skip this block entirely if the sensor is already in Simple mode.
    radar.activateConfigMode();
    radar.setSystemMode(LD2420SystemMode::Simple);
    radar.deactivateConfigMode();

    // ── Callbacks ─────────────────────────────────────────────────────────────

    // Fires only when the presence state changes — not on every frame.
    radar.setPresenceCallback([](bool present) {
        Serial.printf("[PRESENCE] %s\n", present ? "DETECTED" : "CLEAR");
    });

    // Fires every frame that contains a distance reading (~10 Hz in motion).
    radar.setDistanceCallback([](uint16_t distanceCm) {
        Serial.printf("[DISTANCE] %u cm\n", distanceCm);
    });

    radar.setUpdateInterval(500);  // Print state at most every 500 ms — adjust as needed, but note that the sensor outputs at ~10 Hz in Simple mode.

    Serial.println("[OK] Ready — waiting for events...\n");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    // update() reads incoming UART bytes, parses frames and fires callbacks.
    // Call it as often as possible — the internal throttle handles the rate.
    radar.update();
}
