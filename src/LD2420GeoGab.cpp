/**
 * @file LD2420GeoGab.cpp
 * @verbatim
  ____             ____       _
 / ___| ___  ___  / ___| __ _| |__
| |  _ / _ \/ _ \| |  _ / _` | '_ \
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/

 * @endverbatim
 * @brief Implementation of the LD2420GeoGab library.
 * @author Gabriel Sieben (GeoGab)
 * @version 1.0.0
 * @date 2026-03
 * @license MIT
 * @details
 * All public API is documented in LD2420GeoGab.h.
 * This file contains the implementation with inline notes explaining
 * non-obvious protocol details and design decisions.
 *
 * ### Key empirical findings (fw v1.6.1)
 * - Register write (CMD 0x0001) → error 0x0001 — registers are read-only.
 * - ABD write (CMD 0x0007) → works, is the sole active config path.
 * - CMD 0x0011 (serial number) → no response, times out.
 * - CMD 0x0013 (read sys param) → echo only, no data — not implemented.
 * - CMD 0x0068 (restart) → works but sends NO ACK response.
 * - No dedicated factory reset CMD — write factory defaults via ABD instead.
 * - Debug mode is continuous — no auto-revert after N frames.
 * - activateConfigMode() requires a single send with dynamic flush.
 * - writeABDConfig() must write one gate at a time with 125 µs delay.
 */

#include "LD2420GeoGab.h"


// =============================================================================
// Error Strings
// =============================================================================

const char *LD2420GeoGab::errorToString(LD2420Error err) {
    switch (err) {
        case LD2420Error::None:            return "None";
        case LD2420Error::Unknown:         return "Unknown";
        case LD2420Error::Timeout:         return "Timeout";
        case LD2420Error::BadResponse:     return "BadResponse";
        case LD2420Error::NotInConfigMode: return "NotInConfigMode";
        default:                           return "?";
    }
}


// =============================================================================
// Lifecycle
// =============================================================================

bool LD2420GeoGab::begin(int txPin, int rxPin, uint32_t baudRate) {
    sensorSerial = &GG_UART_PORT;
    sFlags.initialized = false;

    GG_LOGI("begin()  TX=%d  RX=%d  Baud=%u  S3=%d",
            txPin, rxPin, (unsigned)baudRate, IS_ESP32_S3);

    // NOTE: ESP32 HardwareSerial::begin(baud, config, rxPin, txPin) — RX before TX!
    sensorSerial->begin(baudRate, SERIAL_8N1, rxPin, txPin);
    delay(100); // Give the UART peripheral time to settle before the first byte

    // Enter config mode as a communication check — if this fails the sensor
    // is not responding (wrong pins, wrong baud, power issue).
    LD2420Error err = activateConfigMode();
    if (err != LD2420Error::None) {
        GG_LOGE("begin() failed: %s — check wiring/baud", errorToString(err));
        return false;
    }

    // Cache the firmware version for getFirmwareVersion() — called here so
    // the user can inspect it immediately after begin() without a second
    // config mode session.
    readFirmwareVersion();

    // Leave config mode — the sensor starts emitting detection frames now.
    deactivateConfigMode();

    sFlags.initialized = true;
    GG_LOGI("begin() OK");
    return true;
}

// -----------------------------------------------------------------------------

void LD2420GeoGab::update() {
    // Do not consume bytes from the ring buffer while a synchronous command
    // exchange is in progress — the response parser in waitForResponse()
    // also reads from rxBuffer and the two must not interleave.
    if (sFlags.inConfigMode || sFlags.cmdInFlight) return;

    // Always drain the hardware UART FIFO into the ring buffer, regardless of
    // the throttle timer — we never want to overflow the hardware FIFO.
    while (sensorSerial && sensorSerial->available()) {
        uint8_t b = sensorSerial->read();
        uint16_t next = (rxBuffer.head + 1) % RX_BUF_SIZE;
        if (next != rxBuffer.tail) {           // drop byte if ring buffer is full
            rxBuffer.buf[rxBuffer.head] = b;
            rxBuffer.head = next;
        }
    }

    // Throttle: only parse and dispatch at most once per updateInterval ms.
    runtime.StartTime = millis();
    if (runtime.StartTime - runtime.LastStartTime < settings.updateInterval) return;
    runtime.LastStartTime = runtime.StartTime;

    processRxBuffer();
    processAsyncOperations();

    // Track processing time (useful for tuning updateInterval).
    runtime.StopTime  = millis();
    runtime.FrameTime = runtime.StopTime - runtime.StartTime;
#if GG_DEBUG >= 2
    GG_LOGI("Frame processed in %lu ms", runtime.FrameTime);
#endif
}

// -----------------------------------------------------------------------------

void LD2420GeoGab::processAsyncOperations() {
    // ── Pre-calibration delay ────────────────────────────────────────────────
    // Waiting for the room to empty before we start collecting frames.
    if (pFlags.calDelayActive) {
        if (millis() >= autoCalib.delayDeadline) {
            pFlags.calDelayActive = false;
            pFlags.calCollecting  = true;
            GG_LOGI("AutoCalib: delay done — collecting %u frames", autoCalib.frameTarget);
        }
        return; // nothing else to do while waiting
    }

    // ── Frame collection ─────────────────────────────────────────────────────
    // Energy frames are fed into autoCalib.accum[] via the internal
    // energy callback hooked up in startAutoCalibration().
    // We just check here whether the target has been reached.
    if (pFlags.calCollecting) {
        if (autoCalib.frameCount >= autoCalib.frameTarget) {
            pFlags.calCollecting = false;
            finishAutoCalibration();
        }
    }
}

void LD2420GeoGab::setUpdateInterval(unsigned long interval) {
    settings.updateInterval = interval;
}


// =============================================================================
// Config Mode
// =============================================================================

LD2420Error LD2420GeoGab::activateConfigMode() {
    uint8_t payload[2];
    writeU16LE(payload, 0, LD2420_PROTOCOL_VERSION);  // value = 0x0002

    // ── Dynamic flush ────────────────────────────────────────────────────────
    // Drain bytes until the sensor has been silent for 200 ms (max 2 s total).
    // A fixed timeout is not enough when the sensor is in Debug mode —
    // 1288-byte frames arrive continuously and refill the UART faster than
    // a fixed window can drain them.
    // NOTE: Empirical testing showed a single ACTIVATE_CONFIG send is sufficient
    // on fw v1.6.1 — the double-send documented in the spec is not required.
    uint32_t flushDeadline = millis() + 2000;
    uint32_t lastByte      = millis();
    while (millis() < flushDeadline) {
        if (sensorSerial->available()) {
            sensorSerial->read();
            lastByte = millis();
        }
        if (millis() - lastByte > 200) break;  // 200 ms silence — sensor has stopped
        delay(1);
    }
    rxBuffer.head = rxBuffer.tail = 0;

    RxFrame resp;
    LD2420Error err = sendCommand(LD2420Cmd::ACTIVATE_CONFIG, payload, 2, resp);
    if (err == LD2420Error::None) {
        sFlags.inConfigMode = true;
        GG_LOGI("Config mode ON (proto v%u, buf %u bytes)",
                readU16LE(resp.data, 2), readU16LE(resp.data, 4));
    } else {
        GG_LOGE("activateConfigMode() failed: %s", errorToString(err));
    }
    return err;
}

// -----------------------------------------------------------------------------

LD2420Error LD2420GeoGab::deactivateConfigMode() {
    RxFrame resp;
    LD2420Error err = sendCommand(LD2420Cmd::DEACTIVATE_CONFIG, nullptr, 0, resp);
    if (err == LD2420Error::None) {
        sFlags.inConfigMode = false;
        GG_LOGI("Config mode OFF");
    }
    return err;
}


// =============================================================================
// Firmware & Serial
// =============================================================================

LD2420Error LD2420GeoGab::readFirmwareVersion() {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    RxFrame resp;
    LD2420Error err = sendCommand(LD2420Cmd::READ_VERSION, nullptr, 0, resp);
    if (err != LD2420Error::None) return err;

    // Response data layout:
    //   [0..1] = status (already checked by sendCommand)
    //   [2..3] = string length (uint16 LE)
    //   [4..]  = ASCII version string, e.g. "v1.6.1"
    if (resp.length < 6) return LD2420Error::BadResponse;
    uint16_t strLen = readU16LE(resp.data, 2);
    if (resp.length < (uint16_t)(4 + strLen)) return LD2420Error::BadResponse;

    // Build the version string character by character (avoids substr allocations).
    values.firmwareVersion.versionStr = "";
    for (uint16_t i = 0; i < strLen; i++)
        values.firmwareVersion.versionStr += (char)resp.data[4 + i];

    // Parse "v1.4.14" → major=1, minor=4, patch=14
    const char *s = values.firmwareVersion.versionStr.c_str();
    if (*s == 'v') s++;                              // skip leading 'v'
    values.firmwareVersion.major = (uint8_t)atoi(s);
    while (*s && *s != '.') s++; if (*s) s++;        // skip to minor
    values.firmwareVersion.minor = (uint8_t)atoi(s);
    while (*s && *s != '.') s++; if (*s) s++;        // skip to patch
    values.firmwareVersion.patch = (uint8_t)atoi(s);

    GG_LOGI("Firmware: %s (%u.%u.%u)",
            values.firmwareVersion.versionStr.c_str(),
            values.firmwareVersion.major,
            values.firmwareVersion.minor,
            values.firmwareVersion.patch);
    return LD2420Error::None;
}

// -----------------------------------------------------------------------------

LD2420Error LD2420GeoGab::readSerialNumber() {
    // NOTE: CMD 0x0011 is NOT implemented in fw v1.6.1.
    // The sensor does not respond — this call will always time out (500 ms).
    // Kept for future firmware compatibility.
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    RxFrame resp;
    LD2420Error err = sendCommand(LD2420Cmd::READ_SERIAL_NUM, nullptr, 0, resp);
    if (err != LD2420Error::None) return err;

    // Expected layout if ever implemented:
    //   [0..1] = status
    //   [2..3] = moduleId (uint16 LE)
    //   [4..7] = serialNumber (uint32 LE)
    if (resp.length < 8) return LD2420Error::BadResponse;
    values.serialInfo.moduleId     = readU16LE(resp.data, 2);
    values.serialInfo.serialNumber = readU32LE(resp.data, 4);

    GG_LOGI("Module ID: 0x%04X  Serial: 0x%08X",
            values.serialInfo.moduleId,
            values.serialInfo.serialNumber);
    return LD2420Error::None;
}


// =============================================================================
// ABD Parameters
// =============================================================================

LD2420Error LD2420GeoGab::writeABDParam(uint16_t paramAddr, uint32_t value) {
    // Delegate to the batch version with count = 1
    return writeABDParams(&paramAddr, &value, 1);
}

LD2420Error LD2420GeoGab::writeABDParams(const uint16_t *paramAddrs,
                                         const uint32_t *values,
                                         uint8_t count) {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    // Payload layout: (2-byte addr + 4-byte value) × N
    // Max count = 16 gates × 6 bytes = 96 bytes — fixed size, no VLA
    uint8_t payload[LD2420_TOTAL_GATES * 6];
    uint16_t off = 0;
    for (uint8_t i = 0; i < count; i++) {
        writeU16LE(payload, off, paramAddrs[i]); off += 2;
        writeU32LE(payload, off, values[i]);     off += 4;
    }
    RxFrame resp;
    return sendCommand(LD2420Cmd::WRITE_ABD_PARAM, payload, off, resp);
}

// -----------------------------------------------------------------------------

LD2420Error LD2420GeoGab::readABDParam(uint16_t paramAddr, uint32_t &outValue) {
    uint32_t val;
    LD2420Error err = readABDParams(&paramAddr, &val, 1);
    if (err == LD2420Error::None) outValue = val;
    return err;
}

LD2420Error LD2420GeoGab::readABDParams(const uint16_t *paramAddrs,
                                        uint32_t *outValues,
                                        uint8_t count) {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    // Payload layout: 2-byte address × N — fixed size, no VLA
    uint8_t payload[LD2420_TOTAL_GATES * 2];
    uint16_t off = 0;
    for (uint8_t i = 0; i < count; i++) {
        writeU16LE(payload, off, paramAddrs[i]); off += 2;
    }
    RxFrame resp;
    LD2420Error err = sendCommand(LD2420Cmd::READ_ABD_PARAM, payload, off, resp);
    if (err != LD2420Error::None) return err;

    // Response layout:
    //   [0..1]        = status
    //   [2..(2+N*4)]  = 4-byte values × N  (little-endian)
    if (resp.length < (uint16_t)(2 + count * 4)) return LD2420Error::BadResponse;
    for (uint8_t i = 0; i < count; i++)
        outValues[i] = readU32LE(resp.data, 2 + i * 4);
    return LD2420Error::None;
}


// =============================================================================
// System Parameters
// =============================================================================

LD2420Error LD2420GeoGab::writeSysParam(uint16_t paramAddr, uint32_t value) {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    // Payload: 2-byte address + 4-byte value
    uint8_t payload[6];
    writeU16LE(payload, 0, paramAddr);
    writeU32LE(payload, 2, value);
    RxFrame resp;
    return sendCommand(LD2420Cmd::WRITE_SYS_PARAM, payload, 6, resp);
}

LD2420Error LD2420GeoGab::readSysParam(uint16_t paramAddr, uint32_t &outValue) {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    // Payload: 2-byte address only
    uint8_t payload[2];
    writeU16LE(payload, 0, paramAddr);
    RxFrame resp;
    LD2420Error err = sendCommand(LD2420Cmd::READ_SYS_PARAM, payload, 2, resp);
    if (err != LD2420Error::None) return err;

    // Response: [0..1]=status, [2..5]=value (uint32 LE)
    if (resp.length < 6) return LD2420Error::BadResponse;
    outValue = readU32LE(resp.data, 2);
    return LD2420Error::None;
}


// =============================================================================
// High-Level Configuration
// =============================================================================

LD2420Error LD2420GeoGab::setSystemMode(LD2420SystemMode mode) {
    GG_LOGI("setSystemMode(%u)", (unsigned)mode);
    return writeSysParam(LD2420SysParam::SYSTEM_MODE, static_cast<uint32_t>(mode));
}

LD2420Error LD2420GeoGab::getSystemMode(LD2420SystemMode &outMode) {
    uint32_t val;
    LD2420Error err = readSysParam(LD2420SysParam::SYSTEM_MODE, val);
    if (err == LD2420Error::None) outMode = static_cast<LD2420SystemMode>(val);
    return err;
}

// -----------------------------------------------------------------------------

LD2420Error LD2420GeoGab::setGateRange(uint8_t minGate, uint8_t maxGate, uint16_t timeoutSec) {
    GG_LOGI("setGateRange(%u, %u, %us)", minGate, maxGate, timeoutSec);

    // Write all three global ABD parameters in one frame for efficiency.
    uint16_t addrs[3]  = { LD2420ABD::ROI_MIN, LD2420ABD::ROI_MAX, LD2420ABD::DELAY_TIME };
    uint32_t values[3] = { minGate, maxGate, timeoutSec };
    return writeABDParams(addrs, values, 3);
}

LD2420Error LD2420GeoGab::getGateRange(uint16_t &outMin, uint16_t &outMax, uint16_t &outTimeout) {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    // Read the three global ABD parameters in one frame.
    uint16_t addrs[3] = { LD2420ABD::ROI_MIN, LD2420ABD::ROI_MAX, LD2420ABD::DELAY_TIME };
    uint32_t vals[3]  = {};
    LD2420Error err = readABDParams(addrs, vals, 3);
    if (err != LD2420Error::None) return err;
    outMin     = (uint16_t)vals[0];
    outMax     = (uint16_t)vals[1];
    outTimeout = (uint16_t)vals[2];
    return LD2420Error::None;
}

// -----------------------------------------------------------------------------

LD2420Error LD2420GeoGab::setGateABDHighThreshold(uint8_t gate, uint32_t value) {
    if (gate >= LD2420_TOTAL_GATES) return LD2420Error::Unknown;
    return writeABDParam(LD2420ABD::highThresh(gate), value);
}

LD2420Error LD2420GeoGab::setGateABDLowThreshold(uint8_t gate, uint32_t value) {
    if (gate >= LD2420_TOTAL_GATES) return LD2420Error::Unknown;
    return writeABDParam(LD2420ABD::lowThresh(gate), value);
}

// -----------------------------------------------------------------------------

LD2420Error LD2420GeoGab::readABDConfig(LD2420ABDConfig &outConfig) {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    // ── Global parameters ───────────────────────────────────────────────────
    uint16_t gAddrs[3] = { LD2420ABD::ROI_MIN, LD2420ABD::ROI_MAX, LD2420ABD::DELAY_TIME };
    uint32_t gVals[3]  = {};
    LD2420Error err = readABDParams(gAddrs, gVals, 3);
    if (err != LD2420Error::None) return err;
    outConfig.roiMin = gVals[0]; outConfig.roiMax = gVals[1]; outConfig.delayTime = gVals[2];

    // ── Per-gate thresholds ──────────────────────────────────────────────────
    // Send all 16 high-threshold addresses in one READ_ABD_PARAM frame,
    // then all 16 low-threshold addresses.
    uint16_t hiAddrs[LD2420_TOTAL_GATES], loAddrs[LD2420_TOTAL_GATES];
    uint32_t hiVals[LD2420_TOTAL_GATES],  loVals[LD2420_TOTAL_GATES];
    for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++) {
        hiAddrs[i] = LD2420ABD::highThresh(i);  // 0x0010 + i
        loAddrs[i] = LD2420ABD::lowThresh(i);   // 0x0020 + i
    }

    err = readABDParams(hiAddrs, hiVals, LD2420_TOTAL_GATES);
    if (err != LD2420Error::None) return err;
    for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++) outConfig.highThresh[i] = hiVals[i];

    err = readABDParams(loAddrs, loVals, LD2420_TOTAL_GATES);
    if (err != LD2420Error::None) return err;
    for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++) outConfig.lowThresh[i] = loVals[i];

    return LD2420Error::None;
}

LD2420Error LD2420GeoGab::writeABDConfig(const LD2420ABDConfig &config) {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    // ── Global parameters ───────────────────────────────────────────────────
    uint16_t gAddrs[3] = { LD2420ABD::ROI_MIN, LD2420ABD::ROI_MAX, LD2420ABD::DELAY_TIME };
    uint32_t gVals[3]  = { config.roiMin, config.roiMax, config.delayTime };
    LD2420Error err = writeABDParams(gAddrs, gVals, 3);
    if (err != LD2420Error::None) return err;

    // ── Per-gate thresholds — one gate at a time ─────────────────────────────
    // ESPHome writes one gate per command with a 125 µs delay between each.
    // Sending all 16 gates in a single frame causes a Timeout on fw v1.6.1 —
    // the sensor cannot process such a large payload in one go.
    for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++) {
        delayMicroseconds(125);
        uint16_t hiAddr = LD2420ABD::highThresh(i);
        err = writeABDParam(hiAddr, config.highThresh[i]);
        if (err != LD2420Error::None) return err;

        delayMicroseconds(125);
        uint16_t loAddr = LD2420ABD::lowThresh(i);
        err = writeABDParam(loAddr, config.lowThresh[i]);
        if (err != LD2420Error::None) return err;
    }

    return LD2420Error::None;
}


// =============================================================================
// Auto-Calibration
// =============================================================================

LD2420Error LD2420GeoGab::startAutoCalibration(uint16_t frames,
                                                uint16_t delayMs,
                                                bool     blocking,
                                                bool     skipGate0) {
    if (sFlags.calibrating) {
        GG_LOGW("startAutoCalibration() called while already calibrating — ignored");
        return LD2420Error::Unknown;
    }

    GG_LOGI("AutoCalib: start — frames=%u  delay=%ums  blocking=%d  skipGate0=%d",
            frames, delayMs, blocking, skipGate0);

    // ── Save current state ───────────────────────────────────────────────────
    autoCalib.frameTarget         = frames;
    autoCalib.frameCount          = 0;
    autoCalib.skipGate0           = skipGate0;
    autoCalib.blocking            = blocking;
    autoCalib.savedUpdateInterval = settings.updateInterval;
    autoCalib.savedPresenceCb     = callbacks.presenceCb;
    autoCalib.savedDistanceCb     = callbacks.distanceCb;
    autoCalib.savedStatusCb       = callbacks.statusCb;
    autoCalib.savedEnergyCb       = callbacks.energyCb;
    autoCalib.savedDebugCb        = callbacks.debugCb;
    memset(autoCalib.accum, 0, sizeof(autoCalib.accum));

    // Save current system mode so we can restore it afterwards.
    // If we can't read it, fall back to Energy (safe default).
    if (getSystemMode(autoCalib.savedMode) != LD2420Error::None)
        autoCalib.savedMode = LD2420SystemMode::Energy;

    // ── Switch to Energy mode ────────────────────────────────────────────────
    // Calibration always reads Energy frames regardless of the user's
    // configured output mode.
    activateConfigMode();
    setSystemMode(LD2420SystemMode::Energy);
    deactivateConfigMode();

    // ── Suppress user callbacks, set max update rate ─────────────────────────
    callbacks.presenceCb = nullptr;
    callbacks.distanceCb = nullptr;
    callbacks.statusCb   = nullptr;
    callbacks.debugCb    = nullptr;

    // Hook an internal energy callback to accumulate gate energies.
    callbacks.energyCb = [this](const LD2420EnergyFrame &frame) {
        if (!pFlags.calCollecting) return;
        for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++)
            autoCalib.accum[g] += frame.gateEnergy[g];
        autoCalib.frameCount++;
        GG_LOGI("AutoCalib: frame %u/%u", autoCalib.frameCount, autoCalib.frameTarget);
    };

    // Maximum update rate during calibration for fastest possible collection.
    settings.updateInterval = 10;

    sFlags.calibrating = true;

    // ── Start delay or go straight to collecting ─────────────────────────────
    if (delayMs > 0) {
        autoCalib.delayDeadline = millis() + delayMs;
        pFlags.calDelayActive   = true;
        GG_LOGI("AutoCalib: waiting %u ms for room to empty...", delayMs);
    } else {
        pFlags.calCollecting = true;
        GG_LOGI("AutoCalib: collecting frames immediately");
    }

    // ── Blocking mode ────────────────────────────────────────────────────────
    if (blocking) {
        GG_LOGI("AutoCalib: blocking until done...");
        while (sFlags.calibrating) {
            update();
            delay(1);
        }
    }

    return LD2420Error::None;
}

// -----------------------------------------------------------------------------

void LD2420GeoGab::cancelAutoCalibration() {
    if (!sFlags.calibrating) return;

    GG_LOGI("AutoCalib: cancelled");

    pFlags.calDelayActive = false;
    pFlags.calCollecting  = false;
    sFlags.calibrating    = false;

    // Restore everything
    settings.updateInterval = autoCalib.savedUpdateInterval;
    callbacks.presenceCb    = autoCalib.savedPresenceCb;
    callbacks.distanceCb    = autoCalib.savedDistanceCb;
    callbacks.statusCb      = autoCalib.savedStatusCb;
    callbacks.energyCb      = autoCalib.savedEnergyCb;
    callbacks.debugCb       = autoCalib.savedDebugCb;

    activateConfigMode();
    setSystemMode(autoCalib.savedMode);
    deactivateConfigMode();

    // Fire callback with failure + empty result
    if (callbacks.calibrationCb) {
        LD2420ABDConfig empty;
        callbacks.calibrationCb(false, empty);
    }
}

// -----------------------------------------------------------------------------

void LD2420GeoGab::finishAutoCalibration() {
    GG_LOGI("AutoCalib: computing thresholds from %u frames", autoCalib.frameCount);

    // ── Read current global ABD params (roiMin/roiMax/delayTime) ─────────────
    // We preserve these — only thresholds are updated.
    LD2420ABDConfig result;
    activateConfigMode();
    readABDConfig(result);  // populates roiMin/roiMax/delayTime + existing thresholds

    // ── Compute new thresholds ───────────────────────────────────────────────
    for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++) {
        if (g == 0 && autoCalib.skipGate0) {
            // Keep factory defaults for gate 0 — PCB self-reflection makes
            // the measured energy unreliable for threshold calculation.
            result.highThresh[0] = LD2420_FACTORY_MOVE_THRESH[0];
            result.lowThresh[0]  = LD2420_FACTORY_STILL_THRESH[0];
            GG_LOGI("AutoCalib: gate 0 skipped — using factory defaults");
            continue;
        }

        uint32_t avg = autoCalib.accum[g] / autoCalib.frameCount;
        result.highThresh[g] = (uint32_t)(avg * LD2420_CALIB_HIGH_FACTOR);
        result.lowThresh[g]  = (uint32_t)(avg * LD2420_CALIB_LOW_FACTOR);

        // Safety: ensure lowThresh is always strictly less than highThresh.
        // Can only fail if LD2420_CALIB_LOW_FACTOR >= LD2420_CALIB_HIGH_FACTOR.
        if (result.lowThresh[g] >= result.highThresh[g])
            result.lowThresh[g] = result.highThresh[g] / 2;

        // Enforce a minimum threshold — a computed value of 0 would make the
        // sensor permanently report presence even in an empty room.
        if (result.highThresh[g] == 0) result.highThresh[g] = LD2420_FACTORY_MOVE_THRESH[g];
        if (result.lowThresh[g]  == 0) result.lowThresh[g]  = LD2420_FACTORY_STILL_THRESH[g];

        GG_LOGI("AutoCalib: gate %2u  avg=%lu  high=%lu  low=%lu",
                g, avg, result.highThresh[g], result.lowThresh[g]);
    }

    // ── Write new config and restore mode ────────────────────────────────────
    writeABDConfig(result);
    setSystemMode(autoCalib.savedMode);
    deactivateConfigMode();

    // ── Restore user state ───────────────────────────────────────────────────
    settings.updateInterval = autoCalib.savedUpdateInterval;
    callbacks.presenceCb    = autoCalib.savedPresenceCb;
    callbacks.distanceCb    = autoCalib.savedDistanceCb;
    callbacks.statusCb      = autoCalib.savedStatusCb;
    callbacks.energyCb      = autoCalib.savedEnergyCb;
    callbacks.debugCb       = autoCalib.savedDebugCb;

    sFlags.calibrating = false;

    GG_LOGI("AutoCalib: done — thresholds written, state restored");

    // ── Fire completion callback ──────────────────────────────────────────────
    if (callbacks.calibrationCb) callbacks.calibrationCb(true, result);
}


// =============================================================================
// Factory Operations
// =============================================================================

LD2420Error LD2420GeoGab::factoryReset() {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;
    GG_LOGI("Factory reset — writing factory defaults via ABD...");

    // There is no dedicated factory reset command on fw v1.6.1.
    // ESPHome implements factory reset by writing the factory default values
    // back via ABD write commands — we do the same.
    LD2420ABDConfig defaults;
    defaults.roiMin    = FACTORY_MIN_GATE;
    defaults.roiMax    = FACTORY_MAX_GATE;
    defaults.delayTime = FACTORY_TIMEOUT;
    for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++) {
        defaults.highThresh[i] = LD2420_FACTORY_MOVE_THRESH[i];
        defaults.lowThresh[i]  = LD2420_FACTORY_STILL_THRESH[i];
    }

    LD2420Error err = writeABDConfig(defaults);
    if (err == LD2420Error::None) {
        GG_LOGI("Factory reset complete");
        deactivateConfigMode();  // flush values to sensor flash before reboot
        restart();
    } else {
        GG_LOGE("Factory reset failed: %s", errorToString(err));
    }
    return err;
}

LD2420Error LD2420GeoGab::restart() {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;
    GG_LOGI("Restart...");

    // CMD 0x0068 sends no ACK response on fw v1.6.1 — empirically verified.
    // Simply send the frame and wait for the sensor to reboot.
    uint8_t frameBuf[LD2420_MAX_CMD_BYTES] = {};
    uint8_t frameLen = buildCmdFrame(frameBuf, LD2420Cmd::RESTART, nullptr, 0);
    sendRaw(frameBuf, frameLen);

    sFlags.inConfigMode = false;
    delay(1500); // Allow sensor to finish rebooting
    return LD2420Error::None;
}

LD2420Error LD2420GeoGab::readFactoryTestInfo(LD2420FactoryTestInfo &outInfo) {
    if (!sFlags.inConfigMode) return LD2420Error::NotInConfigMode;

    // NOTE: CMD 0x0024 has not yet been empirically tested on fw v1.6.1.
    RxFrame resp;
    LD2420Error err = sendCommand(LD2420Cmd::FACTORY_TEST_IN, nullptr, 0, resp);
    if (err != LD2420Error::None) return err;

    // Expected response layout (16 bytes of payload):
    //   [0..1]   = status
    //   [2..3]   = subBoardModel
    //   [4..5]   = chipCount
    //   [6..7]   = channelCount
    //   [8..9]   = dataType
    //   [10..11] = fftSize1D
    //   [12..13] = chirpsPerFrame
    //   [14..15] = downsampleInterval
    if (resp.length < 16) return LD2420Error::BadResponse;
    outInfo.subBoardModel      = readU16LE(resp.data, 2);
    outInfo.chipCount          = readU16LE(resp.data, 4);
    outInfo.channelCount       = readU16LE(resp.data, 6);
    outInfo.dataType           = readU16LE(resp.data, 8);
    outInfo.fftSize1D          = readU16LE(resp.data, 10);
    outInfo.chirpsPerFrame     = readU16LE(resp.data, 12);
    outInfo.downsampleInterval = readU16LE(resp.data, 14);
    return LD2420Error::None;
}


// =============================================================================
// Frame Building & Transport
// =============================================================================

uint8_t LD2420GeoGab::buildCmdFrame(uint8_t *buf, uint16_t command,
                                    const uint8_t *payload, uint16_t payloadLen) {
    // Binary frame structure (all values little-endian):
    //   [0..3]           Header:      FD FC FB FA
    //   [4..5]           Data length: 2 (command) + payloadLen
    //   [6..7]           Command word
    //   [8..8+payloadLen] Payload
    //   [last 4]         Footer:      04 03 02 01
    buf[0] = 0xFD; buf[1] = 0xFC; buf[2] = 0xFB; buf[3] = 0xFA;
    writeU16LE(buf, 4, 2 + payloadLen);          // data length field
    writeU16LE(buf, 6, command);                 // command word
    if (payload && payloadLen > 0)
        memcpy(buf + 8, payload, payloadLen);
    uint16_t fOff = 8 + payloadLen;
    buf[fOff] = 0x04; buf[fOff+1] = 0x03; buf[fOff+2] = 0x02; buf[fOff+3] = 0x01;
    return (uint8_t)(fOff + 4);                 // total frame length
}

// -----------------------------------------------------------------------------

void LD2420GeoGab::sendRaw(const uint8_t *buf, uint16_t len) {
    if (!sensorSerial) return;
    hexDump("TX", buf, len);   // no-op unless GG_DEBUG >= 2
    sensorSerial->write(buf, len);
}

// -----------------------------------------------------------------------------

LD2420Error LD2420GeoGab::sendCommand(uint16_t command, const uint8_t *payload,
                                      uint16_t payloadLen, RxFrame &outResponse) {
    sFlags.cmdInFlight = true;
    if (!sensorSerial) { sFlags.cmdInFlight = false; return LD2420Error::Unknown; }

    uint8_t frameBuf[LD2420_MAX_CMD_BYTES] = {};
    uint8_t frameLen = buildCmdFrame(frameBuf, command, payload, payloadLen);

    // Discard any stale bytes before sending — avoids matching an old response.
    while (sensorSerial->available()) sensorSerial->read();
    rxBuffer.head = rxBuffer.tail = 0;

    sendRaw(frameBuf, frameLen);

    // The sensor echoes the command with the high byte OR-ed with 0x01.
    // e.g. CMD 0x0007 → response 0x0107.
    LD2420Error err = waitForResponse(command | 0x0100, outResponse, LD2420_CMD_TIMEOUT_MS);
    sFlags.cmdInFlight = false;
    return err;
}

// -----------------------------------------------------------------------------

LD2420Error LD2420GeoGab::waitForResponse(uint16_t expectedCmd,
                                          RxFrame &outFrame,
                                          uint32_t timeoutMs) {
    uint32_t deadline = millis() + timeoutMs;

    while (millis() < deadline) {
        // Drain hardware FIFO into ring buffer while we wait.
        while (sensorSerial->available()) {
            uint8_t b = sensorSerial->read();
            uint16_t next = (rxBuffer.head + 1) % RX_BUF_SIZE;
            if (next != rxBuffer.tail) { rxBuffer.buf[rxBuffer.head] = b; rxBuffer.head = next; }
        }

        uint16_t avail = (rxBuffer.head >= rxBuffer.tail)
                       ? (rxBuffer.head - rxBuffer.tail)
                       : (RX_BUF_SIZE - rxBuffer.tail + rxBuffer.head);
        if (avail < 10) { delay(1); continue; }  // need at least header + cmd + footer

        // Linearise the ring buffer section into a flat array for easier scanning.
        uint8_t tmp[256];
        uint16_t tmpLen = (avail < sizeof(tmp)) ? avail : sizeof(tmp);
        for (uint16_t i = 0; i < tmpLen; i++)
            tmp[i] = rxBuffer.buf[(rxBuffer.tail + i) % RX_BUF_SIZE];

        // Scan for a valid command response frame:
        //   Header: FD FC FB FA
        //   Length: data len (uint16 LE)
        //   Command word (must match expectedCmd)
        //   Payload (length bytes)
        //   Footer: 04 03 02 01
        for (uint16_t i = 0; i + 9 < tmpLen; i++) {
            if (tmp[i] != 0xFD || tmp[i+1] != 0xFC ||
                tmp[i+2] != 0xFB || tmp[i+3] != 0xFA) continue;  // not a header

            uint16_t dataLen  = readU16LE(tmp, i + 4);
            uint16_t totalLen = 4 + 2 + dataLen + 4;              // header + len_field + data + footer
            if (i + totalLen > tmpLen) break;                      // frame incomplete — wait for more bytes

            // Verify footer bytes
            if (tmp[i+totalLen-4] != 0x04 || tmp[i+totalLen-3] != 0x03 ||
                tmp[i+totalLen-2] != 0x02 || tmp[i+totalLen-1] != 0x01) continue;

            // Check command word matches what we sent (request | 0x0100)
            uint16_t cmd = readU16LE(tmp, i + 6);
            if (cmd != expectedCmd) continue;

            hexDump("RX", tmp + i, totalLen);  // no-op unless GG_DEBUG >= 2

            // Populate the output frame struct.
            outFrame.command = cmd;
            outFrame.length  = dataLen;
            uint16_t copyLen = (dataLen < sizeof(outFrame.data)) ? dataLen : sizeof(outFrame.data);
            memcpy(outFrame.data, tmp + i + 8, copyLen);
            outFrame.status = readU16LE(outFrame.data, 0);  // first 2 bytes of payload = status

            // Advance ring buffer tail past this frame.
            rxBuffer.tail = (rxBuffer.tail + i + totalLen) % RX_BUF_SIZE;

            if (outFrame.status != 0) {
                GG_LOGW("Cmd 0x%04X returned status 0x%04X", cmd, outFrame.status);
                return LD2420Error::Unknown;  // sensor-level error (e.g. register write denied)
            }
            return LD2420Error::None;
        }
        delay(1);
    }

    GG_LOGE("Timeout waiting for response to cmd 0x%04X", expectedCmd);
    return LD2420Error::Timeout;
}


// =============================================================================
// RX Buffer Processing
// =============================================================================

void LD2420GeoGab::processRxBuffer() {
    uint16_t avail = (rxBuffer.head >= rxBuffer.tail)
                   ? (rxBuffer.head - rxBuffer.tail)
                   : (RX_BUF_SIZE - rxBuffer.tail + rxBuffer.head);
    if (avail < 5) return;  // nothing worth parsing yet

    // Linearise the ring buffer into a flat scratch array.
    // This avoids wrap-around complexity in the parsers.
    uint8_t tmp[RX_BUF_SIZE];
    for (uint16_t i = 0; i < avail; i++)
        tmp[i] = rxBuffer.buf[(rxBuffer.tail + i) % RX_BUF_SIZE];

    uint16_t consumed = 0;

    while (consumed + 4 < avail) {
        uint8_t  *p   = tmp + consumed;
        uint16_t  rem = avail - consumed;

        // ── Energy frame: header F4 F3 F2 F1 ─────────────────────────────
        if (p[0] == 0xF4 && p[1] == 0xF3 && p[2] == 0xF2 && p[3] == 0xF1) {
            if (rem < LD2420_ENERGY_FRAME_LEN) break;  // incomplete — wait for more

            // Verify footer at the expected offset (byte 41..44)
            if (p[41] == 0xF8 && p[42] == 0xF7 && p[43] == 0xF6 && p[44] == 0xF5) {
                LD2420EnergyFrame frame;
                if (parseEnergyFrame(p, LD2420_ENERGY_FRAME_LEN, frame)) {
                    dispatchStatus(frame.status, frame.distance);
                    if (callbacks.energyCb) callbacks.energyCb(frame);
                }
                consumed += LD2420_ENERGY_FRAME_LEN;
                continue;
            }
            // Footer mismatch — this is not a valid energy frame at this offset.
            // Fall through to advance by 1 byte and try again.
        }

        // ── Debug frame: header AA BF 10 14 ──────────────────────────────
        if (p[0] == 0xAA && p[1] == 0xBF && p[2] == 0x10 && p[3] == 0x14) {
            if (rem < LD2420_DEBUG_FRAME_LEN) break;  // incomplete — wait for more

            LD2420DebugFrame frame;
            if (parseDebugFrame(p, LD2420_DEBUG_FRAME_LEN, frame)) {
                if (callbacks.debugCb) callbacks.debugCb(frame);
            }
            consumed += LD2420_DEBUG_FRAME_LEN;
            continue;
        }

        // ── Simple mode text line: "ON\r\n", "OFF\r\n", "Range XXXX\r\n" ─
        // Scan forward for a newline to find the end of the line.
        uint16_t lineEnd = 0;
        for (uint16_t j = 0; j + 1 < rem; j++) {
            if (p[j] == '\n') { lineEnd = j + 1; break; }
        }
        if (lineEnd > 0) {
            LD2420DetectionStatus status;
            uint16_t dist = 0;
            if (parseSimpleFrame(p, lineEnd, status, dist)) dispatchStatus(status, dist);
            consumed += lineEnd;
            continue;
        }

        consumed++;  // no recognisable frame start — advance one byte and retry
    }

    rxBuffer.tail = (rxBuffer.tail + consumed) % RX_BUF_SIZE;
}


// =============================================================================
// Frame Parsers
// =============================================================================

bool LD2420GeoGab::parseEnergyFrame(const uint8_t *buf, uint16_t len,
                                    LD2420EnergyFrame &outFrame) {
    if (len < LD2420_ENERGY_FRAME_LEN) return false;

    // Energy frame payload layout (after the 4-byte header):
    //   [4..5]  = 0x0023 (fixed length marker, not used)
    //   [6]     = detection status byte (0=None, 1=Motion, 2=Presence)
    //   [7..8]  = target distance (uint16 LE, centimetres)
    //   [9..40] = 16 gate energies × 2 bytes each (uint16 LE)
    //   [41..44]= footer F8 F7 F6 F5
    outFrame.status   = static_cast<LD2420DetectionStatus>(buf[6]);
    outFrame.distance = readU16LE(buf, 7);
    for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++)
        outFrame.gateEnergy[i] = readU16LE(buf, 9 + i * 2);
    return true;
}

// -----------------------------------------------------------------------------

bool LD2420GeoGab::parseDebugFrame(const uint8_t *buf, uint16_t len,
                                   LD2420DebugFrame &outFrame) {
    if (len < LD2420_DEBUG_FRAME_LEN) return false;

    // Debug frame payload: 20 Doppler cycles × 16 range gates × 4 bytes (uint32 LE)
    // Starts immediately after the 4-byte header.
    uint16_t off = 4;
    for (uint8_t c = 0; c < 20; c++)
        for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++) {
            outFrame.data[c][g] = readU32LE(buf, off); off += 4;
        }
    return true;
}

// -----------------------------------------------------------------------------

bool LD2420GeoGab::parseSimpleFrame(const uint8_t *buf, uint16_t len,
                                    LD2420DetectionStatus &outStatus,
                                    uint16_t &outDistance) {
    if (len < 2) return false;

    // Copy to a null-terminated char array for safe string comparison.
    char line[64] = {};
    memcpy(line, buf, (len < 63) ? len : 63);

    // Text format (from sensor, CRLF terminated):
    //   "ON\r\n"         → Presence (someone present, may be still)
    //   "OFF\r\n"        → None (no target)
    //   "Range XXXX\r\n" → Motion (XXXX = distance in cm, decimal)
    if (strncmp(line, "ON",    2) == 0) { outStatus = LD2420DetectionStatus::Presence; return true; }
    if (strncmp(line, "OFF",   3) == 0) { outStatus = LD2420DetectionStatus::None;     return true; }
    if (strncmp(line, "Range", 5) == 0) {
        outStatus   = LD2420DetectionStatus::Motion;
        outDistance = (uint16_t)atoi(line + 6);  // skip "Range " (6 chars)
        return true;
    }
    return false;  // unrecognised line — ignore
}


bool LD2420GeoGab::newDataAvailable() {
    if (!values.newData) return false;
    values.newData = false;  // self-clearing
    return true;
}

// -----------------------------------------------------------------------------

void LD2420GeoGab::dispatchStatus(LD2420DetectionStatus status, uint16_t distance) {
    // Mark new data available (self-clearing via newDataAvailable()) and count frame
    values.newData = true;
    values.frameCount++;

    // Raw status callback — fired on every frame
    if (callbacks.statusCb) callbacks.statusCb(status);

    // Distance callback — fired only when a non-zero distance is available
    if (distance > 0) {
        values.lastDistance = distance;           // update poll cache
        if (callbacks.distanceCb) callbacks.distanceCb(distance);
    }

    // Presence callback — fired only on state transitions (not every frame)
    bool present    = (status != LD2420DetectionStatus::None);
    bool wasPresent = (values.lastStatus != LD2420DetectionStatus::None);
    if (present != wasPresent && callbacks.presenceCb) callbacks.presenceCb(present);

    // Update cached status for poll getters (getLastStatus / isPresent)
    values.lastStatus = status;
}


// =============================================================================
// Helpers
// =============================================================================

uint16_t LD2420GeoGab::readU16LE(const uint8_t *buf, uint16_t off) {
    return (uint16_t)(buf[off] | ((uint16_t)buf[off+1] << 8));
}

uint32_t LD2420GeoGab::readU32LE(const uint8_t *buf, uint16_t off) {
    return (uint32_t)buf[off]
         | ((uint32_t)buf[off+1] << 8)
         | ((uint32_t)buf[off+2] << 16)
         | ((uint32_t)buf[off+3] << 24);
}

void LD2420GeoGab::writeU16LE(uint8_t *buf, uint16_t off, uint16_t v) {
    buf[off]   =  v       & 0xFF;
    buf[off+1] = (v >> 8) & 0xFF;
}

void LD2420GeoGab::writeU32LE(uint8_t *buf, uint16_t off, uint32_t v) {
    buf[off]   =  v        & 0xFF;
    buf[off+1] = (v >>  8) & 0xFF;
    buf[off+2] = (v >> 16) & 0xFF;
    buf[off+3] = (v >> 24) & 0xFF;
}

// -----------------------------------------------------------------------------

void LD2420GeoGab::hexDump(const char *label, const uint8_t *buf, uint16_t len) {
#if GG_HEXDUMP_ENABLED
    GG_DEBUG_SERIAL.printf("[LD2420][HEX] %s (%u bytes): ", label, len);
    for (uint16_t i = 0; i < len; i++) {
        if (buf[i] < 0x10) GG_DEBUG_SERIAL.print("0");
        GG_DEBUG_SERIAL.print(buf[i], HEX);
        GG_DEBUG_SERIAL.print(" ");
    }
    GG_DEBUG_SERIAL.println();
#else
    // Suppress unused-parameter warnings when hex dump is disabled.
    (void)label; (void)buf; (void)len;
#endif
}
