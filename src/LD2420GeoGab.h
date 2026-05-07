#pragma once
/**
 * @file LD2420GeoGab.h
 * @verbatim
  ____             ____       _     
 / ___| ___  ___  / ___| __ _| |__  
| |  _ / _ \/ _ \| |  _ / _` | '_ \ 
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/ 
 
 * @endverbatim
 * @brief Calibration — LD2420GeoGab library example (PlatformIO)
 * @author Gabriel Sieben (GeoGab)
 * @version 1.0.1
 * @date 2026-05-07
 * @license MIT
 * @brief Main header for the LD2420GeoGab Arduino/PlatformIO library.
 *
 * @details
 * This library provides a full-featured driver for the HLK-LD2420 24 GHz
 * mmWave presence and motion radar sensor by Shenzhen Hi-Link Electronic Co.
 *
 * ## File structure
 * | File                    | Purpose                                        |
 * |-------------------------|------------------------------------------------|
 * | LD2420GeoGab_config.h   | Board/user config (pins, baud rate, debug)     |
 * | LD2420GeoGab_ConTyp.h   | All types, enums, constants, protocol namespaces|
 * | LD2420GeoGab.h          | Public class API (this file)                   |
 * | LD2420GeoGab.cpp        | Implementation                                 |
 *
 * ## Typical usage
 * @code
 * #include <LD2420GeoGab.h>
 *
 * LD2420GeoGab radar;
 *
 * void setup() {
 *     Serial.begin(115200);
 *     if (!radar.begin()) {
 *         Serial.println("Sensor not found!");
 *         while (true);
 *     }
 *
 *     radar.activateConfigMode();
 *     radar.setSystemMode(LD2420SystemMode::Energy);
 *     radar.setGateRange(1, 8, 30);   // 70 cm – 630 cm, 30 s hold-off
 *     radar.deactivateConfigMode();
 *
 *     // Callback style:
 *     radar.setPresenceCallback([](bool present) {
 *         Serial.println(present ? "Present" : "Gone");
 *     });
 * }
 *
 * void loop() {
 *     radar.update();
 *
 *     // Poll style (alternative to callbacks):
 *     if (radar.isPresent())
 *         Serial.println(radar.getLastDistance());
 * }
 * @endcode
 *
 * @version 1.0.0
 * @date 2026-03
 * @copyright MIT License
 */

#include <Arduino.h>
#include <functional>

#include "LD2420GeoGab_config.h"    ///< Board/user config (pins, baud, debug level)
#include "LD2420GeoGab_ConTyp.h"    ///< Protocol types, enums, constants

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class LD2420GeoGab
 * @brief Driver class for the HLK-LD2420 24 GHz mmWave radar sensor.
 *
 * @details
 * Wraps the full binary UART protocol of the LD2420.  All configuration
 * commands require the sensor to be in *config mode* first
 * (see activateConfigMode() / deactivateConfigMode()).
 *
 * The sensor outputs data frames continuously once config mode is left.
 * Call update() in your `loop()` to process incoming frames and fire callbacks.
 *
 * ### Config mode guard pattern
 * @code
 * radar.activateConfigMode();
 * radar.setSystemMode(LD2420SystemMode::Energy);
 * radar.setGateRange(1, 8, 30);
 * radar.deactivateConfigMode();   // ← sensor resumes detection here
 * @endcode
 *
 * ### Empirically verified on fw v1.6.1
 * - Only **ABD parameters** (CMD 0x0007 / 0x0008) are writable and active.
 * - Register write (CMD 0x0001) returns error — registers are read-only relics.
 * - System mode is set via system parameters (CMD 0x0012).
 * - CMD 0x0011 (serial number) is not implemented in fw v1.6.1.
 * - MTT / VS / GR modes behave identically to Simple mode on fw v1.6.1.
 * - Debug mode is continuous — there is no "20 frame" auto-revert.
 */
class LD2420GeoGab {
public:

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * @brief Initialise the library and verify sensor communication.
     *
     * @details
     * Performs the following sequence:
     *  1. Opens the UART port with the given (or default) pins and baud rate.
     *  2. Enters config mode (double-activate per spec) to verify comms.
     *  3. Reads and caches the firmware version string.
     *  4. Leaves config mode — sensor starts detection immediately.
     *
     * Pin and baud defaults come from `LD2420GeoGab_config.h` and can be
     * overridden here or via PlatformIO `build_flags`.
     *
     * ### Portable usage (all Arduino cores)
     * Initialise your serial port first, then pass it to begin():
     * @code
     * // Any core — user owns the serial initialisation:
     * Serial2.begin(115200);   // AVR: Serial1.begin(...) etc.
     * if (!radar.begin(Serial2)) { ... }
     * @endcode
     *
     * ### ESP32 convenience overload
     * On ESP32 / ESP32-S3 you can let the library open the UART for you:
     * @code
     * if (!radar.begin()) { ... }         // use GG_TXPIN / GG_RXPIN defaults
     * if (!radar.begin(17, 18, 115200)) { ... }  // explicit pins
     * @endcode
     *
     * @param serial   Reference to an **already initialised** HardwareSerial port.
     * @return `true` on success, `false` if no response from sensor
     *
     * @note The sensor retains its configuration in internal flash across
     *       power cycles.  You only need to call setSystemMode() / setGateRange()
     *       once, or whenever you want to change the settings.
     */
    bool begin(HardwareSerial &serial);

#if defined(ARDUINO_ARCH_ESP32)
    /**
     * @brief ESP32 convenience overload — opens the UART automatically.
     *
     * @details
     * Calls `GG_UART_PORT.begin(baudRate, SERIAL_8N1, rxPin, txPin)` then
     * delegates to begin(HardwareSerial &serial).  Only available on ESP32 /
     * ESP32-S3 because the extended `begin(baud, config, rx, tx)` signature
     * is ESP32-specific.
     *
     * @param txPin    ESP32 TX pin → sensor RX (default: GG_TXPIN)
     * @param rxPin    ESP32 RX pin ← sensor TX (default: GG_RXPIN)
     * @param baudRate UART baud — 115200 for fw ≥ v1.5.3, 256000 for older
     * @return `true` on success, `false` if no response from sensor
     */
    bool begin(int txPin         = GG_TXPIN,
               int rxPin         = GG_RXPIN,
               uint32_t baudRate = GG_BAUDRATE);
#endif // ARDUINO_ARCH_ESP32

    /**
     * @brief Process incoming UART data and fire registered callbacks.
     *
     * @details
     * Must be called repeatedly in `loop()`.  Each call:
     *  - Reads all available UART bytes into the ring buffer.
     *  - Attempts to parse complete frames (Energy / Simple / Debug).
     *  - Fires the appropriate callbacks for each fully parsed frame.
     *  - Runs processAsyncOperations() at the end.
     *
     * Returns immediately (without processing) if a config command is in
     * flight or if the sensor is currently in config mode — this prevents
     * detection frames from corrupting the command/response exchange.
     *
     * Processing is throttled to at most once per `settings.updateInterval`
     * milliseconds (default 10 ms).  Adjust with setUpdateInterval().
     */
    void update();

    /**
     * @brief Set the minimum interval between successive processing cycles.
     *
     * @details
     * `update()` always reads raw UART bytes regardless of this setting, but
     * ring-buffer parsing and callback dispatch are throttled to once per
     * `interval` ms.  The sensor itself outputs at ~10 Hz in Energy mode,
     * so values below ~10 ms rarely add benefit.
     *
     * @param interval Minimum processing interval in milliseconds (default: 10)
     */
    void setUpdateInterval(unsigned long interval);

    /**
     * @brief Execute any pending asynchronous internal operations.
     *
     * @details
     * Called automatically at the end of every `update()` cycle.
     * Currently a no-op placeholder reserved for future use
     * (e.g. deferred reboot, auto-calibration triggers).
     */
    void processAsyncOperations();


    // =========================================================================
    // Config Mode
    // =========================================================================

    /**
     * @brief Enter configuration mode.
     *
     * @details
     * Required before any `set*`, `get*`, `read*`, or `write*` config command.
     *
     * Implements the **mandatory double-activate + dynamic flush** sequence
     * from the HI-Link protocol spec:
     *  1. Send ACTIVATE_CONFIG (CMD 0x00FF) with protocol version = 2.
     *  2. Drain bytes until the sensor has been silent for 200 ms (max 2 s).
     *     A fixed timeout is not sufficient when the sensor is in Debug mode —
     *     1288-byte frames arrive continuously and refill the UART faster than
     *     a fixed window can drain them.
     *  3. Send ACTIVATE_CONFIG a second time — this one gets the ACK.
     *
     * @return LD2420Error::None on success
     *
     * @warning The sensor does **not** output detection frames while in config
     *          mode.  Always pair with deactivateConfigMode() when done.
     */
    LD2420Error activateConfigMode();

    /**
     * @brief Leave configuration mode and resume normal detection.
     *
     * @details
     * Sends DEACTIVATE_CONFIG (CMD 0x00FE).  The sensor immediately resumes
     * outputting detection frames in the currently configured system mode.
     *
     * @return LD2420Error::None on success
     */
    LD2420Error deactivateConfigMode();

    /**
     * @brief Query whether the sensor is currently in config mode.
     * @return `true` if config mode is active
     */
    bool isInConfigMode() const { return sFlags.inConfigMode; }


    // =========================================================================
    // Firmware & Serial Info
    // =========================================================================

    /**
     * @brief Read and cache the firmware version string from the sensor.
     *
     * @details
     * Sends CMD 0x0000.  The response is an ASCII string such as `"v1.6.1"`
     * which is parsed into major / minor / patch integers and stored in
     * `values.firmwareVersion`.
     *
     * Called automatically during begin() — use getFirmwareVersion() to
     * retrieve the cached result afterwards.
     *
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error readFirmwareVersion();

    /**
     * @brief Attempt to read the module ID and serial number from the sensor.
     *
     * @details
     * Sends CMD 0x0011.  **Not implemented in fw v1.6.1** — the sensor does
     * not respond and the call will time out after 500 ms.
     * Kept for future firmware versions.
     *
     * @return LD2420Error::Timeout on fw v1.6.1 (expected behaviour)
     * @pre Sensor must be in config mode.
     *
     * @warning Avoid calling in normal operation — always times out on
     *          current firmware and adds a 500 ms blocking delay.
     */
    LD2420Error readSerialNumber();

    /**
     * @brief Return the cached firmware version (populated by begin()).
     *
     * @details
     * The version string `"v1.6.1"` is parsed into:
     * - `versionStr` → `"v1.6.1"`
     * - `major`      → `1`
     * - `minor`      → `6`
     * - `patch`      → `1`
     *
     * @return Const reference to the cached LD2420FirmwareVersion struct.
     *
     * @code
     * Serial.println(radar.getFirmwareVersion().versionStr);  // "v1.6.1"
     * if (radar.getFirmwareVersion().major < 2) { ... }
     * @endcode
     */
    const LD2420FirmwareVersion& getFirmwareVersion() const { return values.firmwareVersion; }

    /**
     * @brief Return the cached serial info (populated by readSerialNumber()).
     *
     * @details
     * On fw v1.6.1 this always returns zeroes because readSerialNumber()
     * is not supported.
     *
     * @return Const reference to the cached LD2420SerialInfo struct.
     */
    const LD2420SerialInfo& getSerialInfo() const { return values.serialInfo; }


    // =========================================================================
    // Poll Getters  (no config mode required — updated every update() call)
    // =========================================================================

    /**
     * @brief Returns true if a new frame has been received since the last call.
     *
     * @details
     * Set to true by dispatchStatus() on every successfully parsed frame.
     * Automatically cleared (self-resetting) on each call — so calling this
     * twice in a row will return true then false if no new frame arrived.
     *
     * Useful for polling without a millis() throttle:
     * @code
     * void loop() {
     *     radar.update();
     *     if (radar.newDataAvailable()) {
     *         Serial.println(radar.getLastDistance());
     *     }
     * }
     * @endcode
     *
     * @return true once per new frame, then false until the next frame arrives
     */
    bool newDataAvailable();

    /**
     * @brief Returns the total number of frames successfully parsed since begin().
     *
     * @details
     * Incremented by dispatchStatus() on every parsed Energy, Simple or Debug frame.
     * Useful for detecting missed frames or measuring effective frame rate:
     * @code
     * static uint32_t lastCount = 0;
     * uint32_t count = radar.getFrameCount();
     * if (count != lastCount) {
     *     Serial.printf("New frame — total: %lu  missed: %lu\n",
     *                   count, count - lastCount - 1);
     *     lastCount = count;
     * }
     * @endcode
     *
     * @return Cumulative frame count (wraps at 2^32 ≈ 4 billion frames)
     */
    uint32_t getFrameCount() const { return values.frameCount; }

    /**
     * @brief Return the detection status from the most recently parsed frame.
     *
     * @details
     * Updated by update() on every successfully parsed Energy or Simple frame.
     * Use this instead of (or alongside) setStatusCallback() for a polling
     * style rather than event-driven callbacks.
     *
     * @return LD2420DetectionStatus::None / Motion / Presence
     */
    LD2420DetectionStatus getLastStatus() const { return values.lastStatus; }

    /**
     * @brief Return the distance in cm from the most recently parsed frame.
     *
     * @details
     * The value is the raw sensor output — the distance to the strongest
     * detected target gate.  Not filtered or averaged.
     *
     * In Simple mode the sensor reports the gate centre distance.
     * In Energy mode the sensor reports a finer internal estimate.
     *
     * @return Distance in cm, or 0 if no distance has been received yet.
     */
    uint16_t getLastDistance() const { return values.lastDistance; }

    /**
     * @brief Convenience check — true if any target is currently detected.
     *
     * @details
     * Equivalent to `getLastStatus() != LD2420DetectionStatus::None`.
     * Returns true for both Motion (active movement) and Presence (still person).
     *
     * @return `true` if presence or motion was detected in the last frame
     */
    bool isPresent() const { return values.lastStatus != LD2420DetectionStatus::None; }

    /**
     * @brief Return the full Energy frame from the most recently parsed Energy frame.
     *
     * @details
     * Updated by update() on every successfully parsed Energy frame.
     * Contains status, distance and all 16 gate energies.
     * Use this for polling instead of (or alongside) setEnergyCallback().
     *
     * @return Const reference to the cached LD2420EnergyFrame.
     *
     * @code
     * void loop() {
     *     radar.update();
     *     if (radar.newDataAvailable()) {
     *         const auto &f = radar.getLastEnergyFrame();
     *         Serial.printf("dist=%u cm  gate0=%u\n", f.distance, f.gateEnergy[0]);
     *     }
     * }
     * @endcode
     */
    const LD2420EnergyFrame& getLastEnergyFrame() const { return values.lastEnergyFrame; }


    // =========================================================================
    // ABD Parameters  (low-level access — prefer high-level helpers below)
    // =========================================================================

    /**
     * @brief Write a single ABD parameter to the sensor.
     *
     * @details
     * ABD (Automatic Background Detection) parameters are the **only writable
     * configuration path** on fw v1.6.1.  Register write (CMD 0x0001) returns
     * an error on this firmware — registers are legacy/relict values only.
     *
     * Prefer the high-level helpers (setGateRange, setGateABDHighThreshold, …)
     * unless you need direct access to a specific address.
     *
     * @param paramAddr ABD parameter address (use LD2420ABD::* constants)
     * @param value     32-bit value to write
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error writeABDParam(uint16_t paramAddr, uint32_t value);

    /**
     * @brief Write multiple ABD parameters in a single UART frame.
     *
     * @details
     * More efficient than calling writeABDParam() in a loop — all parameters
     * are sent in one frame and the sensor processes them atomically.
     *
     * @param paramAddrs Array of ABD parameter addresses (LD2420ABD::*)
     * @param values     Array of 32-bit values (same length as paramAddrs)
     * @param count      Number of parameters
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error writeABDParams(const uint16_t *paramAddrs,
                               const uint32_t *values,
                               uint8_t count);

    /**
     * @brief Read a single ABD parameter from the sensor.
     *
     * @param paramAddr ABD parameter address (use LD2420ABD::* constants)
     * @param outValue  Receives the 32-bit value on success
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error readABDParam(uint16_t paramAddr, uint32_t &outValue);

    /**
     * @brief Read multiple ABD parameters in a single UART frame.
     *
     * @param paramAddrs Array of ABD parameter addresses
     * @param outValues  Array to receive the values (same length as paramAddrs)
     * @param count      Number of parameters
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error readABDParams(const uint16_t *paramAddrs,
                              uint32_t *outValues,
                              uint8_t count);


    // =========================================================================
    // System Parameters  (low-level access)
    // =========================================================================

    /**
     * @brief Write a system parameter (CMD 0x0012).
     *
     * @details
     * System parameters control sensor-level behaviour such as the output mode.
     * Use setSystemMode() for the common case.
     *
     * @param paramAddr System parameter address (use LD2420SysParam::* constants)
     * @param value     32-bit value to write
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error writeSysParam(uint16_t paramAddr, uint32_t value);

    /**
     * @brief Read a system parameter (CMD 0x0013).
     *
     * @param paramAddr System parameter address (use LD2420SysParam::* constants)
     * @param outValue  Receives the 32-bit value on success
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error readSysParam(uint16_t paramAddr, uint32_t &outValue);


    // =========================================================================
    // High-Level Configuration
    // =========================================================================

    /**
     * @brief Set the sensor output / operating mode.
     *
     * @details
     * Controls what kind of data the sensor sends in normal detection mode.
     *
     * | Mode            | Value  | Output                               | Notes                   |
     * |-----------------|--------|--------------------------------------|-------------------------|
     * | Energy          | 0x0004 | Binary frames: gate energies + dist  | **Recommended**         |
     * | Simple          | 0x0064 | Text: ON / OFF / Range XXXX          | Low bandwidth           |
     * | Debug           | 0x0000 | Raw Doppler frames 20×16×4 B         | Continuous, high volume |
     * | MTT / VS / GR   | 1/2/3  | Same as Simple on fw v1.6.1          | LD2450 features, no-ops |
     *
     * @param mode Desired operating mode
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     *
     * @note The mode is stored in the sensor's internal flash and survives power cycles.
     */
    LD2420Error setSystemMode(LD2420SystemMode mode);

    /**
     * @brief Read the current operating mode from the sensor.
     *
     * @param outMode Receives the current LD2420SystemMode on success
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     *
     * @note **Not implemented on fw v1.6.1** — CMD 0x0013 (READ_SYS_PARAM)
     *       returns an echo of the request without any data payload.
     *       This function will always return LD2420Error::Timeout on fw v1.6.1.
     *       Kept for future firmware compatibility.
     *       Use setSystemMode() to write the mode — you always know what you wrote.
     */
    LD2420Error getSystemMode(LD2420SystemMode &outMode);

    /**
     * @brief Set the detection gate range and presence hold-off timeout.
     *
     * @details
     * Each gate covers 70 cm.  Limiting the range avoids false positives
     * from walls or furniture outside the area of interest.
     *
     * Gate 0 (0–70 cm) is best excluded — the sensor's own PCB generates
     * strong near-field reflections that reliably cause false triggers.
     *
     * @code
     * radar.setGateRange(1, 8, 30);
     * // → detect from  70 cm (gate 1 start)
     * // →         to  630 cm (gate 8 end  = 9 × 70 cm)
     * // → hold "present" 30 s after last detection
     * @endcode
     *
     * Internally writes three ABD parameters in one frame:
     * - `LD2420ABD::ROI_MIN`    ← minGate
     * - `LD2420ABD::ROI_MAX`    ← maxGate
     * - `LD2420ABD::DELAY_TIME` ← timeoutSec
     *
     * @param minGate    First gate to evaluate (0–15, recommend ≥ 1)
     * @param maxGate    Last gate to evaluate  (0–15, max practical ~11)
     * @param timeoutSec Presence hold-off in seconds after last detection
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     *
     * @note Persists in sensor flash across power cycles.
     */
    LD2420Error setGateRange(uint8_t minGate, uint8_t maxGate, uint16_t timeoutSec);

    /**
     * @brief Read the current gate range and timeout from the sensor.
     *
     * @details
     * Reads the three global ABD parameters (ROI_MIN, ROI_MAX, DELAY_TIME)
     * in a single batched read command.
     *
     * @param outMin     Receives the current minimum gate
     * @param outMax     Receives the current maximum gate
     * @param outTimeout Receives the current hold-off timeout in seconds
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error getGateRange(uint16_t &outMin, uint16_t &outMax, uint16_t &outTimeout);

    /**
     * @brief Set the ABD motion (trigger) threshold for a single gate.
     *
     * @details
     * The sensor reports **Motion** when the reflected signal energy in a gate
     * exceeds this threshold.  Higher value = less sensitive to movement.
     *
     * Factory defaults (see `LD2420_FACTORY_MOVE_THRESH[]`):
     * - Gate 0: 60000  (very high — strong near-field reflection)
     * - Gate 1: 30000
     * - Gates 2–15: 400
     *
     * Tuning guideline: set to ~5× the measured background noise level
     * for that gate (visible in Energy mode via the energy callback).
     *
     * @param gate  Gate index (0–15)
     * @param value Motion trigger threshold in sensor energy units (uint32_t)
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error setGateABDHighThreshold(uint8_t gate, uint32_t value);

    /**
     * @brief Set the ABD presence (maintain) threshold for a single gate.
     *
     * @details
     * The sensor reports **Presence** (still person) when energy exceeds this
     * lower threshold after Motion was detected — maintaining "occupied" even
     * for micro-movements like breathing or small hand gestures.
     *
     * **Must always be lower than the corresponding highThresh for the same gate.**
     *
     * Factory defaults (see `LD2420_FACTORY_STILL_THRESH[]`):
     * - Gate 0: 40000
     * - Gate 1: 20000
     * - Gates 2–7: 150–200
     * - Gates 8–15: 100
     *
     * Tuning guideline: set to ~2–5× the measured background noise level.
     *
     * @param gate  Gate index (0–15)
     * @param value Presence threshold in sensor energy units (uint32_t)
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error setGateABDLowThreshold(uint8_t gate, uint32_t value);

    /**
     * @brief Read the complete ABD configuration from the sensor.
     *
     * @details
     * Reads: roiMin, roiMax, delayTime + all 16 highThresh + all 16 lowThresh
     * using three batched read commands.
     *
     * Useful for displaying the current config, logging, or read-back
     * verification after writeABDConfig().
     *
     * @param outConfig Receives the full configuration on success
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     */
    LD2420Error readABDConfig(LD2420ABDConfig &outConfig);

    /**
     * @brief Write a complete ABD configuration block to the sensor.
     *
     * @details
     * Writes all ABD parameters in three batched write commands:
     * global params, all 16 highThresh, all 16 lowThresh.
     *
     * To restore factory defaults:
     * @code
     * LD2420ABDConfig cfg;
     * cfg.roiMin    = FACTORY_MIN_GATE;   // 0
     * cfg.roiMax    = FACTORY_MAX_GATE;   // 15
     * cfg.delayTime = FACTORY_TIMEOUT;    // 120 s
     * for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++) {
     *     cfg.highThresh[i] = LD2420_FACTORY_MOVE_THRESH[i];
     *     cfg.lowThresh[i]  = LD2420_FACTORY_STILL_THRESH[i];
     * }
     * radar.activateConfigMode();
     * radar.writeABDConfig(cfg);
     * radar.deactivateConfigMode();
     * @endcode
     *
     * @param config ABD configuration to write
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     * @note All values are stored in sensor flash and survive power cycles.
     */
    LD2420Error writeABDConfig(const LD2420ABDConfig &config);


    // =========================================================================
    // Factory Operations
    // =========================================================================

    /**
     * @brief Perform a full factory reset.
     *
     * @details
     * Restores all sensor parameters to Hi-Link factory defaults and reboots.
     * After this call config mode is cleared; the library waits 1500 ms
     * for the sensor to finish rebooting.
     *
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     *
     * @warning The factory reset command code (CMD 0x00A2) has **not yet been
     *          empirically verified** on fw v1.6.1.  Test before relying on
     *          this in production.
     */
    LD2420Error factoryReset();

    /**
     * @brief Reboot the sensor (soft restart, CMD 0x0068).
     *
     * @details
     * The sensor reboots with its stored configuration intact.
     * Config mode is cleared by this call.
     * The library waits 1500 ms for the sensor to finish rebooting.
     *
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     *
     * @note This command has **not yet been empirically tested** on fw v1.6.1.
     */
    LD2420Error restart();

    /**
     * @brief Read hardware factory test info from the sensor (CMD 0x0024).
     *
     * @details
     * Returns chip count, channel count, FFT size, chirps per frame, etc.
     *
     * @param outInfo Receives the factory test info on success
     * @return LD2420Error::None on success
     * @pre Sensor must be in config mode.
     *
     * @note This command has **not yet been empirically tested** on fw v1.6.1.
     */
    LD2420Error readFactoryTestInfo(LD2420FactoryTestInfo &outInfo);


    // =========================================================================
    // Callbacks
    // =========================================================================

    /**
     * @brief Register a callback fired on presence state transitions only.
     *
     * @details
     * Invoked only when the state **changes** — i.e. on the transition from
     * "no target" → "target detected" and back.  Does **not** fire on every
     * frame.  Works in both Energy and Simple modes.
     *
     * @param cb `void(bool present)` — `true` on appearance, `false` on departure
     *
     * @code
     * radar.setPresenceCallback([](bool present) {
     *     digitalWrite(LED_PIN, present ? HIGH : LOW);
     * });
     * @endcode
     */
    void setPresenceCallback(LD2420PresenceCb cb) { callbacks.presenceCb = cb; }

    /**
     * @brief Register a callback fired with the target distance on every frame.
     *
     * @details
     * Fired whenever a frame contains a non-zero distance (~10 Hz in Energy
     * mode).  Distance is the raw sensor output in centimetres.
     *
     * @param cb `void(uint16_t distanceCm)`
     */
    void setDistanceCallback(LD2420DistanceCb cb) { callbacks.distanceCb = cb; }

    /**
     * @brief Register a callback fired with the raw detection status on every frame.
     *
     * @details
     * Finer granularity than the presence callback — distinguishes
     * `None`, `Motion` (active movement), and `Presence` (still person).
     * Fired on every successfully parsed frame (~10 Hz in Energy mode).
     *
     * @param cb `void(LD2420DetectionStatus status)`
     */
    void setStatusCallback(LD2420StatusCb cb) { callbacks.statusCb = cb; }

    /**
     * @brief Register a callback fired for each parsed Energy Output frame.
     *
     * @details
     * Provides full access to the raw gate energy array — useful for
     * calibration, bar graph visualisation or custom detection algorithms.
     * Only relevant in `LD2420SystemMode::Energy`.
     *
     * @param cb `void(const LD2420EnergyFrame &frame)`
     *
     * @code
     * radar.setEnergyCallback([](const LD2420EnergyFrame &f) {
     *     for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++)
     *         Serial.printf("Gate %2u: %u\n", g, f.gateEnergy[g]);
     * });
     * @endcode
     */
    void setEnergyCallback(LD2420EnergyCb cb) { callbacks.energyCb = cb; }

    /**
     * @brief Register a callback fired for each parsed Debug (RDMap) frame.
     *
     * @details
     * Debug mode outputs 20 Doppler cycles × 16 gates × 4 bytes = 1280 bytes
     * per frame, continuously.  Only relevant in `LD2420SystemMode::Debug`.
     *
     * @param cb `void(const LD2420DebugFrame &frame)`
     *
     * @warning Debug frames are ~1.3 KB and arrive continuously at high rate.
     *          Avoid heavy processing inside this callback.
     */
    void setDebugCallback(LD2420DebugCb cb) { callbacks.debugCb = cb; }


    // =========================================================================
    // Auto-Calibration
    // =========================================================================

    /**
     * @brief Start automatic threshold calibration.
     *
     * @details
     * Measures the background noise floor over `frames` Energy output frames
     * and computes new ABD thresholds based on the average gate energies:
     * - `highThresh[g] = avg[g] × LD2420_CALIB_HIGH_FACTOR` (5×, motion trigger)
     * - `lowThresh[g]  = avg[g] × LD2420_CALIB_LOW_FACTOR`  (2×, presence maintain)
     *
     * The calibrated thresholds are written to the sensor via writeABDConfig().
     * The existing roiMin / roiMax / delayTime settings are preserved.
     *
     * ### Sequence
     * 1. Wait `delayMs` milliseconds — time for the person to leave the room.
     * 2. Temporarily switch to Energy mode (restored afterwards).
     * 3. Suppress all user callbacks to avoid spurious events during measurement.
     * 4. Collect `frames` Energy frames and accumulate per-gate energies.
     * 5. Compute and write new thresholds.
     * 6. Restore original mode, callbacks and update interval.
     * 7. Fire calibrationCompleteCb (if registered) with the computed config.
     *
     * ### Blocking vs. non-blocking
     * - `blocking = true`  — function does not return until calibration is done.
     *                        Safe to call from setup(). Callback still fires.
     * - `blocking = false` — function returns immediately (default).
     *                        Calibration runs inside processAsyncOperations().
     *                        **Requires** setCalibrationCompleteCallback() to
     *                        know when the result is ready.
     *
     * ### Gate 0 note
     * Gate 0 (0–70 cm) typically shows very high energy from the sensor's own
     * PCB reflections. By default (`skipGate0 = true`) gate 0 retains its
     * factory default thresholds and is not calibrated. Set `skipGate0 = false`
     * to include it — useful if the sensor is ceiling-mounted far from any
     * surface within 70 cm.
     *
     * @param frames     Number of Energy frames to average (default: 100 ≈ 10 s)
     * @param delayMs    Delay before measurement starts in ms (default: 5000 ms)
     * @param blocking   true = wait until done, false = async via processAsyncOperations()
     * @param skipGate0  true = keep factory defaults for gate 0 (default: true)
     * @return LD2420Error::None if calibration started (or completed in blocking mode)
     *
     * @note Make sure the detection zone is **empty** during calibration.
     *       If a person is present the thresholds will be set too high and
     *       the sensor will not detect presence reliably afterwards.
     */
    LD2420Error startAutoCalibration(uint16_t frames    = 100,
                                     uint16_t delayMs   = 5000,
                                     bool     blocking  = false,
                                     bool     skipGate0 = true);

    /**
     * @brief Cancel a running non-blocking auto-calibration.
     *
     * @details
     * Immediately stops frame collection, restores all saved state
     * (mode, callbacks, update interval) and fires the calibration
     * complete callback with `success = false` and an empty result.
     *
     * Has no effect if no calibration is running.
     */
    void cancelAutoCalibration();

    /**
     * @brief Returns true if auto-calibration is currently in progress.
     * @return `true` while startAutoCalibration() is running (blocking or non-blocking)
     */
    bool isCalibrating() const { return sFlags.calibrating; }

    /**
     * @brief Register a callback fired when auto-calibration completes or is cancelled.
     *
     * @param cb `void(bool success, const LD2420ABDConfig &result)`
     *
     * @code
     * radar.setCalibrationCompleteCallback([](bool ok, const LD2420ABDConfig &cfg) {
     *     if (ok) {
     *         Serial.println("Calibration done!");
     *         for (uint8_t g = 0; g < LD2420_TOTAL_GATES; g++)
     *             Serial.printf("  Gate %2u: high=%lu  low=%lu\n",
     *                           g, cfg.highThresh[g], cfg.lowThresh[g]);
     *     } else {
     *         Serial.println("Calibration cancelled.");
     *     }
     * });
     * @endcode
     */
    void setCalibrationCompleteCallback(LD2420CalibrationCb cb) { callbacks.calibrationCb = cb; }

    /**
     * @brief Convert an error code to a human-readable C-string.
     *
     * @param err Error code to convert
     * @return Pointer to a static string, e.g. `"Timeout"`, `"None"`, …
     */
    static const char *errorToString(LD2420Error err);


// =============================================================================
// PRIVATE — internal implementation details
// =============================================================================
private:

    // ── Hardware ──────────────────────────────────────────────────────────────

    HardwareSerial *sensorSerial = nullptr; ///< Pointer to the active UART port

    // ── Settings ──────────────────────────────────────────────────────────────

    struct {
        uint16_t updateInterval = 10; ///< Minimum ms between processing cycles (default 10 ms)
    } settings;

    // ── Runtime Metrics ───────────────────────────────────────────────────────

    /// Timing data for processing performance monitoring (visible via GG_DEBUG)
    struct {
        uint32_t StartTime     = 0; ///< millis() at start of current update() cycle
        uint32_t StopTime      = 0; ///< millis() at end of current update() cycle
        uint32_t LastStartTime = 0; ///< millis() at start of previous cycle — used for interval throttle
        uint32_t FrameTime     = 0; ///< Duration of last processing cycle in ms
    } runtime;

    // ── Ring Buffer ───────────────────────────────────────────────────────────

    /// Ring buffer capacity. Must exceed the largest expected frame:
    /// Debug frame = 1288 B → 1400 B provides comfortable headroom.
    static constexpr uint16_t RX_BUF_SIZE = 1400;

    /// Lock-free single-producer / single-consumer ring buffer for UART RX bytes.
    /// - head advances when bytes are written (in update() or waitForResponse())
    /// - tail advances when bytes are consumed (in processRxBuffer())
    struct {
        uint8_t  buf[RX_BUF_SIZE] = {};
        uint16_t head = 0; ///< Write index (next free slot)
        uint16_t tail = 0; ///< Read index  (oldest unprocessed byte)
    } rxBuffer;

    // ── Response Frame ────────────────────────────────────────────────────────

    /// Parsed command response frame — populated by waitForResponse()
    struct RxFrame {
        uint8_t  data[256] = {}; ///< Raw payload bytes (after header + command word)
        uint16_t length    = 0;  ///< Payload length in bytes
        uint16_t command   = 0;  ///< Command word echoed from request (with bit 8 set)
        uint16_t status    = 0;  ///< Sensor status: 0 = OK, non-zero = error code
    } rxFrame;

    // ── State Flags ───────────────────────────────────────────────────────────

    struct {
        bool initialized  = false; ///< True after a successful begin()
        bool inConfigMode = false; ///< True while the sensor is in config mode
        bool cmdInFlight  = false; ///< True while sendCommand() awaits a response — blocks update()
        bool calibrating  = false; ///< True while auto-calibration is in progress
    } sFlags;

    /// Flags for deferred async internal operations — driven by processAsyncOperations()
    struct {
        bool calDelayActive = false; ///< Waiting for the pre-calibration delay to expire
        bool calCollecting  = false; ///< Actively collecting Energy frames for calibration
    } pFlags;

    // ── Auto-Calibration State ────────────────────────────────────────────────

    /// All state needed to run a calibration across multiple update() cycles
    struct {
        uint16_t         frameTarget         = 0;     ///< Total frames to collect
        uint16_t         frameCount          = 0;     ///< Frames collected so far
        uint32_t         delayDeadline       = 0;     ///< millis() deadline for pre-calibration delay
        bool             skipGate0           = true;  ///< Keep factory defaults for gate 0
        bool             blocking            = false; ///< true = startAutoCalibration() blocks until done
        uint32_t         accum[LD2420_TOTAL_GATES] = {}; ///< Accumulated gate energy sums
        // ── Saved state — restored after calibration ──────────────────────
        LD2420SystemMode savedMode           = LD2420SystemMode::Energy;
        uint16_t         savedUpdateInterval = 0;
        LD2420PresenceCb savedPresenceCb     = nullptr;
        LD2420DistanceCb savedDistanceCb     = nullptr;
        LD2420StatusCb   savedStatusCb       = nullptr;
        LD2420EnergyCb   savedEnergyCb       = nullptr;
        LD2420DebugCb    savedDebugCb        = nullptr;
    } autoCalib;

    // ── Cached Values (updated by dispatchStatus on every frame) ──────────────

    struct {
        LD2420FirmwareVersion firmwareVersion;                           ///< Populated by readFirmwareVersion() in begin()
        LD2420SerialInfo      serialInfo;                                ///< Populated by readSerialNumber() — always zero on fw v1.6.1
        LD2420DetectionStatus lastStatus   = LD2420DetectionStatus::None; ///< Status from the most recent parsed frame
        uint16_t              lastDistance = 0;                          ///< Distance (cm) from the most recent parsed frame
        LD2420EnergyFrame     lastEnergyFrame;                           ///< Full Energy frame from the most recent parsed Energy frame
        bool                  newData      = false;                      ///< Set on every new frame, cleared by newDataAvailable()
        uint32_t              frameCount   = 0;                          ///< Total frames parsed since begin()
    } values;

    // ── Callbacks ─────────────────────────────────────────────────────────────

    struct {
        LD2420PresenceCb    presenceCb    = nullptr; ///< State-change callback: present / gone
        LD2420DistanceCb    distanceCb    = nullptr; ///< Per-frame distance callback
        LD2420StatusCb      statusCb      = nullptr; ///< Per-frame raw status callback
        LD2420EnergyCb      energyCb      = nullptr; ///< Per Energy-frame callback
        LD2420DebugCb       debugCb       = nullptr; ///< Per Debug-frame callback
        LD2420CalibrationCb calibrationCb = nullptr; ///< Auto-calibration complete callback
    } callbacks;

    // ── Frame Building & Transport ────────────────────────────────────────────

    /**
     * @brief Assemble a binary command frame into `buf`.
     * @details Frame layout (little-endian):
     *   [FD FC FB FA] [len 2B] [cmd 2B] [payload N B] [04 03 02 01]
     * @return Total frame length in bytes.
     */
    uint8_t buildCmdFrame(uint8_t *buf, uint16_t command,
                          const uint8_t *payload, uint16_t payloadLen);

    /**
     * @brief Send a command frame and block until the matching response arrives.
     * @details Sets cmdInFlight=true for the duration; clears it before returning.
     *          The response command word is always (request | 0x0100).
     */
    LD2420Error sendCommand(uint16_t command,
                            const uint8_t *payload, uint16_t payloadLen,
                            RxFrame &outResponse);

    /**
     * @brief Write raw bytes to the UART hardware.
     * @details Also calls hexDump() when GG_DEBUG >= 2.
     */
    void sendRaw(const uint8_t *buf, uint16_t len);

    /**
     * @brief Poll the ring buffer until a matching response frame arrives or timeout.
     * @param expectedCmd Response command word to look for (request | 0x0100)
     * @param outFrame    Populated with the parsed response on success
     * @param timeoutMs   Maximum wait time in milliseconds
     */
    LD2420Error waitForResponse(uint16_t expectedCmd, RxFrame &outFrame,
                                uint32_t timeoutMs = LD2420_CMD_TIMEOUT_MS);

    // ── Frame Parsing ─────────────────────────────────────────────────────────

    /**
     * @brief Scan the ring buffer and dispatch any complete frames found.
     * @details Recognises and handles:
     *   - Energy frames  (header: F4 F3 F2 F1, 45 bytes)
     *   - Debug frames   (header: AA BF 10 14, 1288 bytes)
     *   - Simple frames  (text lines: "ON", "OFF", "Range XXXX")
     *
     *  Advances rxBuffer.tail past all consumed bytes.
     */
    void processRxBuffer();

    /** @brief Parse a 45-byte Energy Output frame. Returns false if malformed. */
    bool parseEnergyFrame(const uint8_t *buf, uint16_t len, LD2420EnergyFrame &outFrame);

    /** @brief Parse a 1288-byte Debug (RDMap) frame. Returns false if malformed. */
    bool parseDebugFrame (const uint8_t *buf, uint16_t len, LD2420DebugFrame  &outFrame);

    /**
     * @brief Parse a Simple mode text line.
     * @details Handles "ON\r\n", "OFF\r\n", "Range XXXX\r\n".
     */
    bool parseSimpleFrame(const uint8_t *buf, uint16_t len,
                          LD2420DetectionStatus &outStatus, uint16_t &outDistance);

    // ── Helpers ───────────────────────────────────────────────────────────────

    static uint16_t readU16LE (const uint8_t *buf, uint16_t offset); ///< Read uint16 little-endian
    static uint32_t readU32LE (const uint8_t *buf, uint16_t offset); ///< Read uint32 little-endian
    static void     writeU16LE(uint8_t *buf, uint16_t offset, uint16_t value); ///< Write uint16 little-endian
    static void     writeU32LE(uint8_t *buf, uint16_t offset, uint32_t value); ///< Write uint32 little-endian

    /**
     * @brief Finish calibration: compute thresholds, write to sensor, restore state.
     * @details Called internally when frameCount reaches frameTarget.
     */
    void finishAutoCalibration();

    /**
     * @brief Hex-dump `len` bytes of `buf` to GG_DEBUG_SERIAL.
     * @details No-op unless GG_DEBUG >= 2 (GG_HEXDUMP_ENABLED is set).
     */
    void hexDump(const char *label, const uint8_t *buf, uint16_t len);

    /**
     * @brief Update cached state and fire the appropriate callbacks.
     * @details Called by processRxBuffer() after every successfully parsed frame.
     *          presenceCb is fired only on state transitions, not every frame.
     *          distanceCb is fired only when distance > 0.
     */
    void dispatchStatus(LD2420DetectionStatus status, uint16_t distance);
};
