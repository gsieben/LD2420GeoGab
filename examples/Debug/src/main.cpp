/**
 * @file main.cpp
 * @verbatim
  ____             ____       _     
 / ___| ___  ___  / ___| __ _| |__  
| |  _ / _ \/ _ \| |  _ / _` | '_ \ 
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/ 
 
 * @endverbatim
 * @brief Debug — LD2420GeoGab library example (PlatformIO)
 * @author Gabriel Sieben (GeoGab)
 * @version 1.0
 * @date 2026-03
 * @copyright license MIT
 * @details
 * Demonstrates Debug output mode (raw RDMap frames) via setDebugCallback().
 *
 * In Debug mode the sensor streams a 2D Doppler × Range matrix continuously:
 *   - 20 Doppler cycles × 16 range gates × 4 bytes = 1280 bytes of data
 *   - Total frame size: 1288 bytes (including 4-byte header and footer)
 *   - Frame rate: as fast as the sensor can produce them (~10–20 Hz)
 *
 * Each cell frame.data[dopplerCycle][gate] is a uint32_t representing the
 * raw signal energy at that Doppler velocity bin and range gate.
 *
 * This example prints:
 *   - A per-gate column sum (sum across all 20 Doppler bins) — useful for
 *     seeing which gates have activity without printing all 320 values.
 *   - The peak Doppler bin per gate — indicates movement velocity.
 *
 * @warning Debug frames are large (~1.3 KB) and arrive continuously.
 *          The RX_BUF_SIZE in the library (1400 B) fits exactly one frame.
 *          Keep the callback fast — heavy Serial printing may cause
 *          the ring buffer to overflow at high baud rates.
 *          Consider using GG_DEBUG=0 to suppress library logging overhead.
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
 
// Block height: 1 header + 1 divider + 16 gate lines + 1 blank = 19 lines
static constexpr uint8_t DEBUG_BLOCK_LINES = 19;
static bool blockDrawn = false;
static uint32_t frameCount = 0;
 
// ─── Setup ────────────────────────────────────────────────────────────────────
 
void setup() {
    Serial.begin(115200);
    delay(500);
 
    Serial.println(GG_CLEAR_SCREEN);
    Serial.println(GG_CYAN "=== LD2420GeoGab Debug ===" GG_RES);
    Serial.println(GG_YELLOW "WARNING: Debug mode streams large frames continuously." GG_RES);
    Serial.println(GG_YELLOW "         Requires ANSI terminal (PlatformIO colorize filter).\n" GG_RES);
 
    if (!radar.begin()) {
        Serial.println(GG_RED "[ERROR] Could not communicate with LD2420 — check wiring/baud." GG_RES);
        while (true) delay(1000);
    }
 
    Serial.printf("Firmware: " GG_CYAN "%s" GG_RES "\n\n",
                  radar.getFirmwareVersion().versionStr.c_str());
 
    radar.activateConfigMode();
    radar.setSystemMode(LD2420SystemMode::Debug);
    radar.deactivateConfigMode();
 
    // ── Debug callback ────────────────────────────────────────────────────────
    radar.setDebugCallback([](const LD2420DebugFrame &frame) {
        frameCount++;
 
        // Compute per-gate average energy and peak Doppler bin.
        // Average (÷20) keeps values in uint32_t range safely.
        uint32_t avgEnergy[LD2420_TOTAL_GATES] = {};
        uint8_t  peakBin[LD2420_TOTAL_GATES]   = {};
 
        for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++) {
            uint32_t sum    = 0;
            uint32_t maxVal = 0;
            for (uint8_t d = 0; d < 20; d++) {
                sum += frame.data[d][g] / 20;   // divide before adding — no overflow
                if (frame.data[d][g] > maxVal) {
                    maxVal     = frame.data[d][g];
                    peakBin[g] = d;
                }
            }
            avgEnergy[g] = sum;
        }
 
        // Move cursor up to overwrite previous block after first draw
        if (blockDrawn)
            Serial.printf(GG_CSI "%uA", DEBUG_BLOCK_LINES);
 
        // Header line
        Serial.printf(GG_CLEAR_LINE "  " GG_CYAN "Gate │   AvgEnergy │ PeakBin │ Bar (scale: 500/bar)" GG_RES
                      "   frame #%05lu\n", frameCount);
        Serial.println(GG_CLEAR_LINE "  ─────┼────────────┼─────────┼───────────────────────────────");
 
        for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++) {
            uint8_t bars = (uint8_t)min(avgEnergy[g] / 500UL, 30UL);
            const char *barColor = (bars < 10) ? GG_GREEN : (bars < 20) ? GG_YELLOW : GG_RED;
 
            Serial.printf(GG_CLEAR_LINE "  G%02u  │ %10lu │   D%02u   │ %s",
                          g, avgEnergy[g], peakBin[g], barColor);
            for (uint8_t b = 0; b < bars;  b++) Serial.print("█");
            Serial.print(GG_RES);
            for (uint8_t b = bars; b < 30; b++) Serial.print(" ");
            Serial.println("|");
        }
        Serial.println(GG_CLEAR_LINE); // blank line — part of block
        blockDrawn = true;
    });
 
    Serial.println("[OK] Streaming — press reset to stop.\n");
}
 
// ─── Loop ─────────────────────────────────────────────────────────────────────
 
void loop() {
    radar.update();
}