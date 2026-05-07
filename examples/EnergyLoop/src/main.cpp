/**
 * @file main.cpp
 * @verbatim
  ____             ____       _     
 / ___| ___  ___  / ___| __ _| |__  
| |  _ / _ \/ _ \| |  _ / _` | '_ \ 
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/ 
 
 * @endverbatim
 * @brief Energy Polling — LD2420GeoGab library test / development scratch pad
 * @author Gabriel Sieben (GeoGab)
 * @version 1.0
 * @date 2026-05
 * @copyright license MIT
 * @details
 * Demonstrates Energy output mode using the **polling** (loop) style —
 * no callbacks are registered. Instead, getLastEnergyFrame() is called
 * in loop() every time newDataAvailable() returns true.
 *
 * In Energy mode the sensor sends a binary frame at ~10 Hz containing:
 *   - Detection status (None / Motion / Presence)
 *   - Distance to strongest target (cm)
 *   - Raw signal energy for all 16 gates (70 cm each)
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
    Serial.println("=== LD2420GeoGab Energy (Polling) ===");

    if (!radar.begin()) {
        Serial.println("[ERROR] Could not communicate with LD2420 — check wiring/baud.");
        while (true) delay(1000);
    }

    Serial.printf("Firmware: %s\n\n", radar.getFirmwareVersion().versionStr.c_str());

    // Switch to Energy output mode.
    radar.activateConfigMode();
    radar.setSystemMode(LD2420SystemMode::Energy);
    radar.deactivateConfigMode();

    // Process at most once every 100 ms — sensor outputs at ~10 Hz.
    radar.setUpdateInterval(100);

    // No callbacks registered — we poll in loop() instead.

    Serial.println("[OK] Ready — polling for frames...\n");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    radar.update();

    if (!radar.newDataAvailable()) return;

    const LD2420EnergyFrame &f = radar.getLastEnergyFrame();

    Serial.printf("[ENERGY]  status=%-8s  dist=%u cm\n",
                  statusLabel(f.status), f.distance);

    for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++)
        printEnergyBar(g, f.gateEnergy[g]);

    Serial.println();
}
