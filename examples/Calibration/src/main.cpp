/**
 * @file main.cpp
 * @verbatim
  ____             ____       _     
 / ___| ___  ___  / ___| __ _| |__  
| |  _ / _ \/ _ \| |  _ / _` | '_ \ 
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/ 
 
 * @endverbatim
 * @brief Calibration — LD2420GeoGab library example (PlatformIO)
 * @author Gabriel Sieben (GeoGab)
 * @version 1.0
 * @date 2024-06-01
 * @license MIT
 * @details
 * Interactive serial-controlled calibration and monitoring tool.
 *
 * Open the serial monitor at 115200 baud and use the following keys:
 *
 *   [e] — switch to Energy mode  (live gate bar graph)
 *   [s] — switch to Simple mode  (single-line presence output)
 *   [c] — start auto-calibration (blocking, 5 s room-empty delay)
 *   [x] — cancel running calibration
 *   [r] — set gate range (roiMin / roiMax / timeout)
 *   [p] — print current ABD configuration
 *   [?] — show help
 *
 * ### Energy mode display
 * Updates a fixed 19-line block in place using ANSI cursor control —
 * the terminal scrolls less and the gate bars are easy to read.
 * Requires a terminal that supports ANSI escape codes (PlatformIO
 * serial monitor with the `colorize` filter, Serial Studio, etc.).
 *
 * ### Simple mode display
 * Overwrites a single line in place using \r — minimal output.
 *
 * ### Auto-calibration
 * Runs in blocking mode: the function does not return until calibration
 * is complete.  After calibration the computed thresholds are printed
 * and the sensor resumes the previously active output mode.
 *
 * Wiring:
 *   ESP32 / ESP32-S3    LD2420
 *   GG_TXPIN        ──  RX
 *   GG_RXPIN        ──  TX
 *   3.3 V           ──  VCC
 *   GND             ──  GND
 *
 * -+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+
 * _  __  __ _____  ____ _____  _____  ____   __  _  _____
 *| ||  \/  || ()_)/ () \| () )|_   _|/ () \ |  \| ||_   _| o
 *|_||_|\/|_||_|   \____/|_|\_\  |_| /__/\__\|_|\__|  |_|   o
 *
 * You strongly need an ANSI supporting terminal for this example
 *
 * (PlatformIO serial monitor with the `colorize` filter, Serial Studio, etc.)
 * to display the energy bars and status labels correctly.
 * If your terminal does not support ANSI escape codes, the output will be
 * cluttered with raw escape sequences and may be hard to read.
 *
 * The debug mode of the library (GG_DEBUG > 0) also interferes with the
 * output, so make sure to set GG_DEBUG=0 in config.h or platformio.ini.
 *
 * -+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+-+H+
 */

#include <Arduino.h>
#include <LD2420GeoGab.h>

LD2420GeoGab radar;

// ─── State ────────────────────────────────────────────────────────────────────

enum class DisplayMode { Energy, Simple };
static DisplayMode displayMode = DisplayMode::Energy;

// Energy display: track whether the fixed block has been drawn once already
// so we know whether to move the cursor up or just print fresh.
static bool energyBlockDrawn = false;

// Number of lines in the Energy display block (status line + 16 gate lines + 1 blank)
static constexpr uint8_t ENERGY_BLOCK_LINES = 18;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static const char *statusLabel(LD2420DetectionStatus s) {
    switch (s) {
        case LD2420DetectionStatus::Motion:   return GG_YELLOW "MOTION  " GG_RES;
        case LD2420DetectionStatus::Presence: return GG_GREEN  "PRESENCE" GG_RES;
        default:                              return GG_DIM     "NONE    " GG_RES;
    }
}

/**
 * @brief Print the 16-gate energy bar graph block.
 * @details If the block was already drawn, moves the cursor up ENERGY_BLOCK_LINES
 *          lines first so the output overwrites in place.
 *          Scale: each bar character = 500 energy units, max 30 bars.
 */
static void printEnergyBlock(const LD2420EnergyFrame &f) {
    if (energyBlockDrawn) {
        // Move cursor up to overwrite the previous block
        Serial.printf(GG_CSI "%uA", ENERGY_BLOCK_LINES);
    }

    // ── Status + distance line ─────────────────────────────────────────────
    Serial.printf(GG_CLEAR_LINE "  Status: %s   Distance: " GG_CYAN "%4u cm" GG_RES
                  "  (gate " GG_CYAN "%u" GG_RES ")\n",
                  statusLabel(f.status),
                  f.distance,
                  distanceToGate(f.distance));

    // ── Gate bars ──────────────────────────────────────────────────────────
    for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++) {
        uint8_t bars = (uint8_t)min((uint32_t)(f.gateEnergy[g] / 500), (uint32_t)30);

        // Colour the bar: green = low, yellow = medium, red = high
        const char *barColor = (bars < 10) ? GG_GREEN : (bars < 20) ? GG_YELLOW : GG_RED;

        Serial.printf(GG_CLEAR_LINE "  G%02u [" GG_CYAN "%5u" GG_RES "] |%s",
                      g, f.gateEnergy[g], barColor);
        for (uint8_t b = 0; b < bars;  b++) Serial.print("█");
        Serial.print(GG_RES);
        for (uint8_t b = bars; b < 30; b++) Serial.print("░");
        Serial.println("|");
    }

    Serial.println(); // blank line — part of the block
    energyBlockDrawn = true;
}

/**
 * @brief Print a single-line Simple mode status (overwrites in place).
 */
static void printSimpleLine(LD2420DetectionStatus status, uint16_t distance) {
    Serial.printf("\r" GG_CLEAR_LINE "  %s   %4u cm  (gate %u)   ",
                  statusLabel(status),
                  distance,
                  distanceToGate(distance));
}

/**
 * @brief Print the full ABD configuration table to Serial.
 */
static void printABDConfig(const LD2420ABDConfig &cfg) {
    Serial.println();
    Serial.println(GG_CYAN "  ┌──────┬────────────────┬────────────────┬────────────────┐" GG_RES);
    Serial.println(GG_CYAN "  │ Gate │  Start–End(cm) │   highThresh   │   lowThresh    │" GG_RES);
    Serial.println(GG_CYAN "  ├──────┼────────────────┼────────────────┼────────────────┤" GG_RES);
    for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++) {
        bool active = (g >= cfg.roiMin && g <= cfg.roiMax);
        Serial.printf("  │  %s%2u%s  │  %4u – %4u   │  %10lu    │  %10lu    │%s\n",
                      active ? GG_CYAN : GG_DIM,
                      g,
                      GG_RES,
                      gateStartCm(g),
                      (g + 1) * LD2420_GATE_SIZE_CM,
                      cfg.highThresh[g],
                      cfg.lowThresh[g],
                      active ? GG_YELLOW " ◄" GG_RES : "");
    }
    Serial.println(GG_CYAN "  └──────┴────────────────┴────────────────┴────────────────┘" GG_RES);
    Serial.printf("  roiMin=%lu  roiMax=%lu  delayTime=%lu s\n\n",
                  cfg.roiMin, cfg.roiMax, cfg.delayTime);
}

/**
 * @brief Print the command help screen.
 */
static void printHelp() {
    Serial.println();
    Serial.println(GG_CYAN "  ┌─────────────────────────────────────────────────────────┐" GG_RES);
    Serial.println(GG_CYAN "  │              LD2420GeoGab Calibration Tool              │" GG_RES);
    Serial.println(GG_CYAN "  ├─────────────────────────────────────────────────────────┤" GG_RES);
    Serial.println(GG_CYAN "  │" GG_RES "  [e]  Switch to Energy mode (gate bar graph)           " GG_CYAN "│" GG_RES);
    Serial.println(GG_CYAN "  │" GG_RES "  [s]  Switch to Simple mode (single line)              " GG_CYAN "│" GG_RES);
    Serial.println(GG_CYAN "  │" GG_RES "  [c]  Start auto-calibration (blocking, 5 s delay)     " GG_CYAN "│" GG_RES);
    Serial.println(GG_CYAN "  │" GG_RES "  [x]  Cancel running calibration                       " GG_CYAN "│" GG_RES);
    Serial.println(GG_CYAN "  │" GG_RES "  [r]  Set gate range (roiMin / roiMax / timeout)       " GG_CYAN "│" GG_RES);
    Serial.println(GG_CYAN "  │" GG_RES "  [p]  Print current ABD configuration                  " GG_CYAN "│" GG_RES);
    Serial.println(GG_CYAN "  │" GG_RES "  [?]  Show this help                                   " GG_CYAN "│" GG_RES);
    Serial.println(GG_CYAN "  └─────────────────────────────────────────────────────────┘" GG_RES);
    Serial.println();
    energyBlockDrawn = false; // force full redraw after help
}

/**
 * @brief Prompt the user to enter a numeric value via Serial.
 * @param prompt  Label shown before the input cursor.
 * @param minVal  Minimum accepted value.
 * @param maxVal  Maximum accepted value.
 * @param current Current value shown as default.
 * @return        Parsed value clamped to [minVal, maxVal].
 */
static uint16_t promptValue(const char *prompt, uint16_t minVal, uint16_t maxVal, uint16_t current) {
    Serial.printf("  %s [%u–%u, current: %u]: ", prompt, minVal, maxVal, current);
    String input = "";
    uint32_t deadline = millis() + 10000; // 10 s input timeout
    while (millis() < deadline) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (input.length() > 0) break;  // only end if we have input
                // otherwise ignore — CR/LF from previous entry still in buffer
            } else if (isDigit(c)) {
                input += c;
                Serial.print(c);
            }
        }
    }
    // Flush any remaining CR/LF from the buffer
    delay(10);
    while (Serial.available()) Serial.read();

    Serial.println();
    if (input.length() == 0) return current;
    uint16_t val = (uint16_t)input.toInt();
    if (val < minVal) val = minVal;
    if (val > maxVal) val = maxVal;
    return val;
}

/**
 * @brief Switch the sensor output mode and update the active display mode.
 */
static void switchMode(LD2420SystemMode mode) {
    radar.activateConfigMode();
    radar.setSystemMode(mode);
    radar.deactivateConfigMode();
    energyBlockDrawn = false;

    if (mode == LD2420SystemMode::Energy) {
        displayMode = DisplayMode::Energy;
        Serial.println("\n  " GG_CYAN "[MODE]" GG_RES " Energy\n");
    } else {
        displayMode = DisplayMode::Simple;
        Serial.println("\n  " GG_CYAN "[MODE]" GG_RES " Simple\n");
    }
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println(GG_CLEAR_SCREEN); // clear terminal on start
    Serial.println(GG_CYAN "  LD2420GeoGab — Calibration Tool" GG_RES);
    Serial.println("  Type [?] for help\n");

    if (!radar.begin()) {
        Serial.println(GG_RED "  [ERROR] Could not communicate with LD2420 — check wiring/baud." GG_RES);
        while (true) delay(1000);
    }

    Serial.printf("  Firmware: " GG_CYAN "%s" GG_RES "\n\n",
                  radar.getFirmwareVersion().versionStr.c_str());

    // Start in Energy mode
    switchMode(LD2420SystemMode::Energy);

    // ── Energy callback — live gate bar graph ─────────────────────────────
    radar.setEnergyCallback([](const LD2420EnergyFrame &frame) {
        if (displayMode == DisplayMode::Energy)
            printEnergyBlock(frame);
    });

    // ── Status callback — Simple mode single line ─────────────────────────
    radar.setStatusCallback([](LD2420DetectionStatus status) {
        if (displayMode == DisplayMode::Simple)
            printSimpleLine(status, radar.getLastDistance());
    });

    // ── Calibration complete callback ─────────────────────────────────────
    radar.setCalibrationCompleteCallback([](bool success, const LD2420ABDConfig &cfg) {
        Serial.println();
        if (success) {
            Serial.println(GG_GREEN "  [CALIB] Complete — new thresholds:" GG_RES);
            printABDConfig(cfg);
        } else {
            Serial.println(GG_RED "  [CALIB] Cancelled." GG_RES);
        }
        energyBlockDrawn = false;
    });
    printHelp();
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    radar.update();

    // ── Serial command handling ───────────────────────────────────────────
    if (!Serial.available()) return;
    char key = Serial.read();

    switch (key) {
        case 'e':
            switchMode(LD2420SystemMode::Energy);
            break;

        case 's':
            switchMode(LD2420SystemMode::Simple);
            break;

        case 'c': {
            // Blocking auto-calibration — function does not return until done.
            // The sensor output and all callbacks are suspended during calibration.
            Serial.println("\n" GG_YELLOW "  [CALIB] Starting — please leave the detection zone!" GG_RES);

            // Countdown so the user has time to step away
            for (int8_t i = 5; i > 0; i--) {
                Serial.printf(GG_CLEAR_LINE "\r  " GG_YELLOW "  Room empty in: %d s..." GG_RES, i);
                delay(1000);
            }
            Serial.println("\r" GG_CLEAR_LINE);

            Serial.println(GG_YELLOW "  [CALIB] Collecting frames..." GG_RES);
            energyBlockDrawn = false;

            // delayMs=0 — we already did our own countdown above
            radar.startAutoCalibration(100, 0, /*blocking=*/true, /*skipGate0=*/true);

            // calibrationCb fires inside startAutoCalibration() before it returns
            break;
        }

        case 'x':
            if (radar.isCalibrating()) {
                radar.cancelAutoCalibration();
            } else {
                Serial.println(GG_DIM "  [x] No calibration running." GG_RES);
            }
            break;

        case 'r': {
            // Read current range first so we can show it as default
            energyBlockDrawn = false;
            Serial.println("\n  " GG_CYAN "[RANGE]" GG_RES " Enter new gate range:\n");

            radar.activateConfigMode();
            uint16_t curMin = 0, curMax = 0, curTimeout = 0;
            radar.getGateRange(curMin, curMax, curTimeout);
            radar.deactivateConfigMode();

            uint16_t newMin     = promptValue("  roiMin     (gate)", 0, 15, curMin);
            uint16_t newMax     = promptValue("  roiMax     (gate)", newMin, 15, curMax);
            uint16_t newTimeout = promptValue("  delayTime  (s)   ", 1, 3600, curTimeout);

            radar.activateConfigMode();
            LD2420Error err = radar.setGateRange(newMin, newMax, newTimeout);
            radar.deactivateConfigMode();

            if (err == LD2420Error::None) {
                Serial.printf(GG_GREEN "  [OK]" GG_RES " Gate range set: %u–%u  timeout=%u s"
                              "  (%u cm – %u cm)\n\n",
                              newMin, newMax, newTimeout,
                              gateStartCm(newMin),
                              (newMax + 1) * LD2420_GATE_SIZE_CM);
            } else {
                Serial.printf(GG_RED "  [ERROR] setGateRange: %s" GG_RES "\n",
                              LD2420GeoGab::errorToString(err));
            }
            energyBlockDrawn = false;
            break;
        }

        case 'p': {
            Serial.println();
            radar.activateConfigMode();
            LD2420ABDConfig cfg;
            LD2420Error err = radar.readABDConfig(cfg);
            radar.deactivateConfigMode();
            if (err == LD2420Error::None) {
                printABDConfig(cfg);
            } else {
                Serial.printf(GG_RED "  [ERROR] readABDConfig: %s" GG_RES "\n",
                              LD2420GeoGab::errorToString(err));
            }
            energyBlockDrawn = false;
            break;
        }

        case '?':
        default:
            printHelp();
            break;
    }
}
