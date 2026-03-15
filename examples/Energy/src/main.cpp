/**
 * @file main.cpp
 * @verbatim
  ____             ____       _     
 / ___| ___  ___  / ___| __ _| |__  
| |  _ / _ \/ _ \| |  _ / _` | '_ \ 
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/ 
 
 * @endverbatim
 * @brief Energy — LD2420GeoGab library example (PlatformIO)
 * @author Gabriel Sieben (GeoGab)
 * @version 1.0
 * @date 2024-06-01
 * @copyright license MIT
 * @details
 * Demonstrates Energy output mode with all available callbacks.
 *
 * In Energy mode the sensor sends a binary frame at ~10 Hz containing:
 *   - Detection status (None / Motion / Presence)
 *   - Distance to strongest target (cm)
 *   - Raw signal energy for all 16 gates (70 cm each)
 *
 * This example registers all five callbacks:
 *   setPresenceCallback() — state transition only (present ↔ gone)
 *   setDistanceCallback() — every frame with a distance value
 *   setStatusCallback()   — every frame, raw None/Motion/Presence
 *   setEnergyCallback()   — every frame, full gate energy array
 *
 * The energy callback prints a compact ASCII bar graph of all 16 gates —
 * useful for observing the noise floor and tuning thresholds.
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

// ─── Helpers ──────────────────────────────────────────────────────────────────

static const char *statusLabel(LD2420DetectionStatus s) {
    switch (s) {
        case LD2420DetectionStatus::Motion:   return "MOTION";
        case LD2420DetectionStatus::Presence: return "PRESENCE";
        default:                              return "NONE";
    }
}

/**
 * @brief Print a single-line ASCII bar for one gate energy value.
 * @details Scale: each '█' represents 500 energy units, max 20 bars.
 */
static void printEnergyBar(uint8_t gate, uint16_t energy) {
    uint8_t bars = (uint8_t)min((uint32_t)(energy / 500), (uint32_t)20);
    Serial.printf("  G%02u [%5u] |", gate, energy);
    for (uint8_t b = 0; b < bars;  b++) Serial.print("█");
    for (uint8_t b = bars; b < 20; b++) Serial.print("░");
    Serial.println("|");
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== LD2420GeoGab Energy ===");

    if (!radar.begin()) {
        Serial.println("[ERROR] Could not communicate with LD2420 — check wiring/baud.");
        while (true) delay(1000);
    }

    Serial.printf("Firmware: %s\n\n", radar.getFirmwareVersion().versionStr.c_str());

    // Switch to Energy output mode.
    radar.activateConfigMode();                             // Must be in config mode to change settings — or to send other commands
    radar.setSystemMode(LD2420SystemMode::Energy);          // Default Mode ist Simple, so this is only needed if you want to change it.
    radar.deactivateConfigMode();                           // Exit config mode to start detection immediately — or leave in config mode to prevent detection frames from interfering with command/response sequences.

    radar.setUpdateInterval(100);                           // Process at most once every 10 ms (default) — adjust as needed, but note that the sensor outputs at ~10 Hz in Energy mode.

    // ── Callbacks ─────────────────────────────────────────────────────────────

    // Fires only on state transitions — not every frame.
    radar.setPresenceCallback([](bool present) {
        Serial.printf("\n[PRESENCE] %s\n\n", present ? "DETECTED" : "CLEAR");
    });

    // Fires every frame that includes a non-zero distance.
    radar.setDistanceCallback([](uint16_t distanceCm) {
        Serial.printf("[DISTANCE] %u cm  (gate %u)\n",
                      distanceCm, distanceToGate(distanceCm));
    });

    // Fires every frame with the raw detection status.
    radar.setStatusCallback([](LD2420DetectionStatus status) {
        Serial.printf("[STATUS]   %s\n", statusLabel(status));
    });

    // Fires every frame with the full gate energy array.
    // Prints a bar graph — reduce GG_DEBUG or comment this out in production.
    radar.setEnergyCallback([](const LD2420EnergyFrame &frame) {
        Serial.printf("[ENERGY]   status=%-8s  dist=%u cm\n",
                      statusLabel(frame.status), frame.distance);
        for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++)
            printEnergyBar(g, frame.gateEnergy[g]);
        Serial.println();
    });

    radar.setUpdateInterval(100);  // Process at most once every 100 ms — adjust as needed, but note that the sensor outputs at ~10 Hz in Energy mode.

    Serial.println("[OK] Ready — waiting for frames...\n");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    radar.update();
}
