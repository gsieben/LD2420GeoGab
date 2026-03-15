#pragma once
/**
 * @file LD2420GeoGab_ConTyp.h
 * @verbatim
  ____             ____       _     
 / ___| ___  ___  / ___| __ _| |__  
| |  _ / _ \/ _ \| |  _ / _` | '_ \ 
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/ 
 
 * @endverbatim
 * @brief Calibration — LD2420GeoGab library example (PlatformIO)
 * @author Gabriel Sieben (GeoGab)
 * @brief Types, constants, enums and protocol namespaces for the LD2420GeoGab library.
 * @version 1.0.0
 * @date 2024-06-01
 * @license MIT
 * @details
 * This file defines everything that the library needs to describe the
 * HLK-LD2420 binary protocol:
 *  - Protocol frame constants (headers, footers, lengths, timeouts)
 *  - Factory default threshold arrays
 *  - Gate distance helper constant
 *  - Command code namespace (LD2420Cmd)
 *  - Register address namespace (LD2420Reg) — legacy, read-only on fw v1.6.1
 *  - ABD parameter address namespace (LD2420ABD) — active config path
 *  - System parameter address namespace (LD2420SysParam)
 *  - All enumerations: LD2420SystemMode, LD2420DetectionStatus, LD2420Error
 *  - All data structures: frames, config blocks, sensor info
 *  - Callback type aliases
 *
 * ## Protocol frame types
 *
 * | Frame type | Header (LE uint32) | Footer (LE uint32) | Length   |
 * |------------|--------------------|--------------------|----------|
 * | Command TX | FD FC FB FA        | 04 03 02 01        | variable |
 * | Command RX | FD FC FB FA        | 04 03 02 01        | variable |
 * | Energy     | F4 F3 F2 F1        | F8 F7 F6 F5        | 45 B     |
 * | Debug      | AA BF 10 14        | FD FC FB FA        | 1288 B   |
 * | Simple     | (ASCII text line)  | \\r\\n              | variable |
 *
 * @version 1.0.0
 * @date 2026-03
 * @copyright MIT License
 */

#include <Arduino.h>
#include <functional>

// ─── Protocol Constants ───────────────────────────────────────────────────────

static constexpr uint8_t  LD2420_TOTAL_GATES        = 16;    ///< Number of detection gates (0–15)
static constexpr uint16_t LD2420_MAX_CMD_BYTES       = 128;  ///< Maximum command frame size — must fit 16 gates × 6 bytes + header/footer overhead
static constexpr uint16_t LD2420_PROTOCOL_VERSION    = 2;    ///< Protocol version sent in ACTIVATE_CONFIG

// Frame sync words (stored little-endian in the byte stream)
static constexpr uint32_t LD2420_CMD_FRAME_HEADER    = 0xFAFBFCFD; ///< Command frame header: FD FC FB FA
static constexpr uint32_t LD2420_CMD_FRAME_FOOTER    = 0x01020304; ///< Command frame footer: 04 03 02 01
static constexpr uint32_t LD2420_ENERGY_FRAME_HEADER = 0xF1F2F3F4; ///< Energy frame header:  F4 F3 F2 F1
static constexpr uint32_t LD2420_ENERGY_FRAME_FOOTER = 0xF5F6F7F8; ///< Energy frame footer:  F8 F7 F6 F5
static constexpr uint32_t LD2420_DEBUG_FRAME_HEADER  = 0x1410BFAA; ///< Debug frame header:   AA BF 10 14
static constexpr uint32_t LD2420_DEBUG_FRAME_FOOTER  = 0xFAFBFCFD; ///< Debug frame footer:   FD FC FB FA

static constexpr uint16_t LD2420_CMD_TIMEOUT_MS      = 500;  ///< Default command response timeout in ms
// Note: activateConfigMode() uses a dynamic flush (200 ms silence) instead
//       of a fixed timeout — no CONFIG_CLEAR_MS constant needed.  ///< Flush delay between the two ACTIVATE_CONFIG sends

/// Energy frame total length in bytes:
/// 4 (header) + 2 (length field) + 1 (status) + 2 (distance) + 32 (16 gates × 2 B) + 4 (footer) = 45
static constexpr uint8_t  LD2420_ENERGY_FRAME_LEN    = 45;

/// Debug frame total length in bytes:
/// 4 (header) + 20×16×4 (Doppler data) + 4 (footer) = 1288
static constexpr uint16_t LD2420_DEBUG_FRAME_LEN     = 1288;


// ─── Factory Default Thresholds ───────────────────────────────────────────────

/**
 * @brief Factory default motion (trigger / high) thresholds for all 16 gates.
 *
 * @details
 * The sensor declares **Motion** when the gate energy exceeds highThresh.
 *
 * Gates 0–1 have very high thresholds because the sensor's own PCB produces
 * strong near-field reflections. Gates 2–15 are at 400 (gentle movement).
 *
 * Per HI-Link manual: recommended trigger threshold ≥ 5× background noise.
 *
 * Available globally after `#include <LD2420GeoGab.h>` — no class prefix.
 *
 * @code
 * radar.activateConfigMode();
 * LD2420ABDConfig cfg;
 * for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++)
 *     cfg.highThresh[i] = LD2420_FACTORY_MOVE_THRESH[i];
 * radar.writeABDConfig(cfg);
 * radar.deactivateConfigMode();
 * @endcode
 */
static constexpr uint32_t LD2420_FACTORY_MOVE_THRESH[LD2420_TOTAL_GATES] = {
    60000, 30000, 400, 250, 250, 250, 250, 250,
    250,   250,   250, 250, 250, 250, 250, 250
};

/**
 * @brief Factory default presence (maintain / low) thresholds for all 16 gates.
 *
 * @details
 * The sensor declares **Presence** (still person / micro-motion) when the gate
 * energy exceeds lowThresh after Motion was detected.  This keeps the "occupied"
 * state for breathing, slight gestures, etc.
 *
 * **Must always be lower than the corresponding MOVE threshold for the same gate.**
 *
 * Per HI-Link manual: recommended presence threshold = 2–5× background noise.
 *
 * Available globally after `#include <LD2420GeoGab.h>` — no class prefix.
 */
static constexpr uint32_t LD2420_FACTORY_STILL_THRESH[LD2420_TOTAL_GATES] = {
    40000, 20000, 200, 200, 200, 200, 200, 150,
    150,   100,   100, 100, 100, 100, 100, 100
};


// ─── Gate Distance Helper ─────────────────────────────────────────────────────

/**
 * @brief Physical width of each detection gate in centimetres.
 *
 * @details
 * The LD2420 divides its detection range into 16 gates (0–15), each covering
 * exactly 70 cm.
 *
 * ```
 * Gate start (cm) = gate × LD2420_GATE_SIZE_CM
 * Gate end   (cm) = (gate + 1) × LD2420_GATE_SIZE_CM
 *
 * Gate  0 =    0 –   70 cm  ← near-field; strong self-reflection → exclude with minGate=1
 * Gate  1 =   70 –  140 cm
 * Gate  5 =  350 –  420 cm
 * Gate  8 =  560 –  630 cm
 * Gate 11 =  770 –  840 cm  ← practical max for wall-mount motion detection (~8 m)
 * Gate 15 = 1050 – 1120 cm  ← theoretical maximum
 * ```
 *
 * Practical limits per HI-Link manual:
 * - Wall mounted:    motion ≤ 8 m (gate 11), micro-motion ≤ 6 m (gate 8)
 * - Ceiling mounted: motion ≤ 5 m (gate 7),  micro-motion ≤ 4 m (gate 5)
 */
static constexpr uint16_t LD2420_GATE_SIZE_CM = 70;

/**
 * @brief Return the start distance (cm) of the given gate.
 * @param gate Gate index (0–15)
 * @return Distance in cm to the near edge of the gate
 * @note gate 1 → 70 cm, gate 8 → 560 cm
 */
inline uint16_t gateStartCm(uint8_t gate) { return gate * LD2420_GATE_SIZE_CM; }

/**
 * @brief Return the centre distance (cm) of the given gate.
 * @param gate Gate index (0–15)
 * @return Distance in cm to the centre of the gate
 * @note gate 1 → 105 cm, gate 8 → 595 cm
 */
inline uint16_t gateCentreCm(uint8_t gate) { return gate * LD2420_GATE_SIZE_CM + LD2420_GATE_SIZE_CM / 2; }

/**
 * @brief Return the gate index that covers the given distance.
 * @param distanceCm Distance in cm (0–1119)
 * @return Gate index (0–15), clamped to valid range
 * @note 105 cm → gate 1, 600 cm → gate 8
 */
inline uint8_t distanceToGate(uint16_t distanceCm) {
    uint8_t g = distanceCm / LD2420_GATE_SIZE_CM;
    return (g < LD2420_TOTAL_GATES) ? g : (LD2420_TOTAL_GATES - 1);
}

/// Factory default minimum gate (0 = 0 cm start)
static constexpr uint16_t FACTORY_MIN_GATE = 0;

/// Factory default maximum gate (15 = 1050–1120 cm)
static constexpr uint16_t FACTORY_MAX_GATE = 15;

/// Factory default presence hold-off timeout in seconds
static constexpr uint16_t FACTORY_TIMEOUT  = 120;


// ─── Command Codes ────────────────────────────────────────────────────────────

/**
 * @namespace LD2420Cmd
 * @brief Raw UART command codes for the HLK-LD2420 protocol.
 *
 * @details
 * All commands are 16-bit values sent as part of the binary frame.
 * The sensor echoes the command in the response with bit 8 set
 * (i.e. response command = request | 0x0100).
 *
 * ### Verified on fw v1.6.1
 * | Command           | Code   | Status                                        |
 * |-------------------|--------|-----------------------------------------------|
 * | READ_VERSION      | 0x0000 | ✅ Works — returns "v1.6.1"                    |
 * | WRITE_REGISTER    | 0x0001 | ❌ Returns error 0x0001 — not writable         |
 * | READ_REGISTER     | 0x0002 | ⚠  Returns values but NOT in sync with ABD    |
 * | WRITE_ABD_PARAM   | 0x0007 | ✅ Works — sole active config write path       |
 * | READ_ABD_PARAM    | 0x0008 | ✅ Works                                       |
 * | READ_SERIAL_NUM   | 0x0011 | ❌ No response (not implemented in fw v1.6.1) |
 * | WRITE_SYS_PARAM   | 0x0012 | ✅ Works — sets system/output mode             |
 * | READ_SYS_PARAM    | 0x0013 | ✅ Works                                       |
 * | FACTORY_TEST_IN   | 0x0024 | ❓ Not yet tested                              |
 * | RESTART           | 0x0068 | ❓ Not yet tested                              |
 * | FACTORY_RESET     | 0x00A2 | ❓ Not yet tested — code is unverified         |
 * | DEACTIVATE_CONFIG | 0x00FE | ✅ Works                                       |
 * | ACTIVATE_CONFIG   | 0x00FF | ✅ Works (double-activate required)            |
 */
namespace LD2420Cmd {
    static constexpr uint16_t READ_VERSION      = 0x0000; ///< Read firmware version string
    static constexpr uint16_t WRITE_REGISTER    = 0x0001; ///< Write register(s) — NOT writable on fw v1.6.1
    static constexpr uint16_t READ_REGISTER     = 0x0002; ///< Read register(s)  — values NOT synced with ABD
    static constexpr uint16_t WRITE_ABD_PARAM   = 0x0007; ///< Write ABD parameter(s) — sole active config write
    static constexpr uint16_t READ_ABD_PARAM    = 0x0008; ///< Read ABD parameter(s)
    static constexpr uint16_t READ_SERIAL_NUM   = 0x0011; ///< Read serial number — not implemented in fw v1.6.1
    static constexpr uint16_t WRITE_SYS_PARAM   = 0x0012; ///< Write system parameter (e.g. output mode)
    static constexpr uint16_t READ_SYS_PARAM    = 0x0013; ///< Read system parameter
    static constexpr uint16_t FACTORY_TEST_IN   = 0x0024; ///< Read factory hardware test info (unverified)
    static constexpr uint16_t FACTORY_TEST_OUT  = 0x0025; ///< Factory test output (unverified)
    static constexpr uint16_t FACTORY_TEST_SEND = 0x0026; ///< Factory test send (unverified)
    static constexpr uint16_t RESTART           = 0x0068; ///< Soft reboot — ✅ verified on fw v1.6.1, sends NO ACK response
    static constexpr uint16_t FACTORY_RESET     = 0x00A2; ///< ❌ Not implemented — use writeABDConfig() with factory defaults instead
    static constexpr uint16_t DEACTIVATE_CONFIG = 0x00FE; ///< Leave config mode
    static constexpr uint16_t ACTIVATE_CONFIG   = 0x00FF; ///< Enter config mode (send twice with 100 ms gap)
}


// ─── Register Addresses ───────────────────────────────────────────────────────

/**
 * @namespace LD2420Reg
 * @brief Register addresses for the HLK-LD2420.
 *
 * @details
 * **⚠ LEGACY / RELICT — Do NOT use for configuration on fw v1.6.1.**
 *
 * Empirical testing showed that:
 * - Register **write** (CMD 0x0001) returns error status 0x0001 — the sensor
 *   simply does not allow writing registers on current firmware.
 * - Register **read**  (CMD 0x0002) works mechanically but returns values that
 *   are **not synchronised** with the active ABD parameters.  For example:
 *   minGate/maxGate read from registers returned 0/0 even after
 *   setGateRange(1, 8, 30) had been applied via ABD.
 *
 * These constants are kept for reference and in case a future firmware version
 * re-enables register writes.  Use LD2420ABD for all active configuration.
 */
namespace LD2420Reg {
    static constexpr uint16_t MIN_GATE             = 0x0000; ///< Minimum detection gate
    static constexpr uint16_t MAX_GATE             = 0x0001; ///< Maximum detection gate
    static constexpr uint16_t TIMEOUT              = 0x0004; ///< Presence hold-off timeout
    static constexpr uint16_t MOTION_THRESH_BASE   = 0x0010; ///< Gate 0 motion threshold; gate N = 0x0010 + N
    static constexpr uint16_t PRESENCE_THRESH_BASE = 0x0020; ///< Gate 0 presence threshold; gate N = 0x0020 + N

    /// @brief Address of the motion threshold register for the given gate (0–15).
    inline uint16_t motionThresh(uint8_t gate)   { return MOTION_THRESH_BASE   + gate; }

    /// @brief Address of the presence threshold register for the given gate (0–15).
    inline uint16_t presenceThresh(uint8_t gate) { return PRESENCE_THRESH_BASE + gate; }
}


// ─── ABD Parameter Addresses ──────────────────────────────────────────────────

/**
 * @namespace LD2420ABD
 * @brief ABD (Automatic Background Detection) parameter addresses.
 *
 * @details
 * **✅ Active configuration path on fw v1.6.1.**
 *
 * All writable sensor configuration goes through ABD parameters
 * (CMD 0x0007 write / CMD 0x0008 read).
 *
 * ### Global parameters
 * | Address | Name       | Description                                    |
 * |---------|------------|------------------------------------------------|
 * | 0x0000  | ROI_MIN    | First gate to evaluate (region of interest min)|
 * | 0x0001  | ROI_MAX    | Last gate to evaluate  (region of interest max)|
 * | 0x0002  | DELAY_TIME | Presence hold-off after last detection (seconds)|
 *
 * ### Per-gate parameters
 * | Address range | Name            | Description                      |
 * |---------------|-----------------|----------------------------------|
 * | 0x0010–0x001F | HIGH_THRESH_BASE| Motion trigger threshold, gate 0–15 |
 * | 0x0020–0x002F | LOW_THRESH_BASE | Presence maintain threshold, gate 0–15 |
 *
 * All values are 32-bit (uint32_t) in little-endian encoding.
 */
namespace LD2420ABD {
    static constexpr uint16_t ROI_MIN          = 0x0000; ///< Global: minimum gate of interest
    static constexpr uint16_t ROI_MAX          = 0x0001; ///< Global: maximum gate of interest
    static constexpr uint16_t DELAY_TIME       = 0x0002; ///< Global: presence hold-off in seconds
    static constexpr uint16_t HIGH_THRESH_BASE = 0x0010; ///< Gate 0 motion threshold; gate N = 0x0010 + N
    static constexpr uint16_t LOW_THRESH_BASE  = 0x0020; ///< Gate 0 presence threshold; gate N = 0x0020 + N

    /// @brief ABD address of the motion (trigger) threshold for the given gate (0–15).
    inline uint16_t highThresh(uint8_t gate) { return HIGH_THRESH_BASE + gate; }

    /// @brief ABD address of the presence (maintain) threshold for the given gate (0–15).
    inline uint16_t lowThresh(uint8_t gate)  { return LOW_THRESH_BASE  + gate; }
}


// ─── System Parameter Addresses ───────────────────────────────────────────────

/**
 * @namespace LD2420SysParam
 * @brief System parameter addresses (CMD 0x0012 write / CMD 0x0013 read).
 *
 * @details
 * System parameters control top-level sensor behaviour.
 * Currently only SYSTEM_MODE is used in normal operation.
 */
namespace LD2420SysParam {
    static constexpr uint16_t SYSTEM_MODE        = 0x0000; ///< Output/operating mode (LD2420SystemMode)
    static constexpr uint16_t UPLOAD_SAMPLE_RATE = 0x0001; ///< Frame upload sample rate (purpose unclear)
    static constexpr uint16_t DEBUG_MODE         = 0x0002; ///< Internal debug mode flag
}


// ─── Enumerations ─────────────────────────────────────────────────────────────

/**
 * @enum LD2420SystemMode
 * @brief Sensor output / operating modes (written via LD2420SysParam::SYSTEM_MODE).
 *
 * @details
 * ### Verified on fw v1.6.1
 * | Mode   | Value  | Output                                    | Notes                          |
 * |--------|--------|-------------------------------------------|--------------------------------|
 * | Debug  | 0x0000 | Raw Doppler frames 20×16×4 B, continuous  | ✅ Verified                    |
 * | MTT    | 0x0001 | ASCII text ON/OFF/Range                   | ✅ Verified — same as Simple   |
 * | VS     | 0x0002 | ASCII text ON/OFF/Range                   | ✅ Verified — same as Simple   |
 * | GR     | 0x0003 | ASCII text ON/OFF/Range                   | ✅ Verified — same as Simple   |
 * | Energy | 0x0004 | Binary gate-energy frames                 | ✅ Verified — **Recommended**  |
 * | Simple | 0x0064 | ASCII text ON/OFF/Range                   | ✅ Verified                    |
 *
 * MTT / VS / GR are LD2450 features that are not implemented in the LD2420
 * firmware — they silently fall back to Simple mode behaviour.
 *
 * Debug mode runs **continuously** with no auto-revert after N frames
 * (contrary to some third-party documentation).
 */
enum class LD2420SystemMode : uint32_t {
    Debug  = 0x0000, ///< Raw Doppler output (20 cycles × 16 gates × 4 B); continuous
    MTT    = 0x0001, ///< Multi-target tracking — alias for Simple on fw v1.6.1
    VS     = 0x0002, ///< Vital signs / fine-motion — alias for Simple on fw v1.6.1
    GR     = 0x0003, ///< Group detection — alias for Simple on fw v1.6.1
    Energy = 0x0004, ///< Gate energies + distance + status — primary recommended mode
    Simple = 0x0064, ///< Basic ASCII presence text (ON / OFF / Range XXXX)
};

/**
 * @enum LD2420DetectionStatus
 * @brief Detection state reported in each sensor frame.
 *
 * @details
 * Carried in the Energy frame's status byte and mapped from Simple mode
 * text output.
 *
 * | Value    | Meaning                                               |
 * |----------|-------------------------------------------------------|
 * | None     | No target detected                                    |
 * | Motion   | Active movement detected (energy > highThresh)        |
 * | Presence | Still person detected (energy > lowThresh after Motion)|
 */
enum class LD2420DetectionStatus : uint8_t {
    None     = 0, ///< No target in the monitored gate range
    Motion   = 1, ///< Active movement — energy exceeded highThresh in at least one gate
    Presence = 2, ///< Still presence — energy exceeded lowThresh (maintained after Motion)
};

/**
 * @enum LD2420Error
 * @brief Return codes for all library functions.
 */
enum class LD2420Error : uint8_t {
    None            = 0x00, ///< Success
    Unknown         = 0x01, ///< Sensor returned a non-zero status code
    Timeout         = 0x02, ///< No response received within LD2420_CMD_TIMEOUT_MS
    BadResponse     = 0x03, ///< Response frame was too short or structurally invalid
    NotInConfigMode = 0x04, ///< Config command was attempted outside of config mode
};


// ─── Data Structures ──────────────────────────────────────────────────────────

/**
 * @struct LD2420FirmwareVersion
 * @brief Parsed firmware version information.
 */
struct LD2420FirmwareVersion {
    String  versionStr;   ///< Raw version string from sensor, e.g. "v1.6.1"
    uint8_t major = 0;    ///< Major version component
    uint8_t minor = 0;    ///< Minor version component
    uint8_t patch = 0;    ///< Patch version component
};

/**
 * @struct LD2420SerialInfo
 * @brief Module ID and serial number.
 * @note Not implemented in fw v1.6.1 — always zero.
 */
struct LD2420SerialInfo {
    uint16_t moduleId     = 0; ///< Module hardware ID
    uint32_t serialNumber = 0; ///< 4-byte serial number
};

/**
 * @struct LD2420EnergyFrame
 * @brief Parsed Energy Output frame (45 bytes, header F4 F3 F2 F1).
 *
 * @details
 * The sensor sends these frames continuously in `LD2420SystemMode::Energy`.
 * Each frame contains the current detection status, the distance to the
 * strongest target, and the raw signal energy for all 16 gates.
 *
 * Layout in the byte stream:
 * ```
 * [F4 F3 F2 F1] [23 00] [PP] [DD DD] [EE EE × 16] [F8 F7 F6 F5]
 *  header         len    status dist   gate energies  footer
 * ```
 */
struct LD2420EnergyFrame {
    LD2420DetectionStatus status = LD2420DetectionStatus::None; ///< Current detection state
    uint16_t distance = 0;                                      ///< Distance to strongest target (cm, raw sensor value)
    uint16_t gateEnergy[LD2420_TOTAL_GATES] = {};               ///< Per-gate signal energy — use for calibration / visualisation
};

/**
 * @struct LD2420DebugFrame
 * @brief Parsed Debug (RDMap) frame (1288 bytes, header AA BF 10 14).
 *
 * @details
 * Debug mode streams a 2D matrix of raw Doppler × Range data.
 * Layout: 20 Doppler measurement cycles, each containing 16 range gate values.
 *
 * Accessing a specific cell: `frame.data[dopplerCycle][gate]`
 *
 * @note These frames are ~1.3 KB and arrive continuously — process efficiently.
 */
struct LD2420DebugFrame {
    uint32_t data[20][LD2420_TOTAL_GATES] = {}; ///< [dopplerCycle][gate] raw 32-bit values
};

/**
 * @struct LD2420ABDConfig
 * @brief Complete ABD configuration block — all writable sensor parameters.
 *
 * @details
 * Used with readABDConfig() and writeABDConfig() to read or write the full
 * sensor configuration in one operation.
 *
 * ### Relationships
 * - `roiMin` / `roiMax` define the active detection zone in gate units (70 cm each)
 * - `delayTime` is the presence hold-off after last detection (seconds)
 * - `highThresh[g]` = motion trigger for gate g (energy > this → Motion)
 * - `lowThresh[g]`  = presence maintain for gate g (energy > this → Presence)
 * - Always ensure `lowThresh[g] < highThresh[g]` for every gate
 */
struct LD2420ABDConfig {
    uint32_t roiMin    = 0; ///< First active gate (ABD ROI_MIN, typically 1)
    uint32_t roiMax    = 0; ///< Last active gate  (ABD ROI_MAX, typically ≤ 11)
    uint32_t delayTime = 0; ///< Presence hold-off in seconds (ABD DELAY_TIME)
    uint32_t highThresh[LD2420_TOTAL_GATES] = {}; ///< Per-gate motion trigger thresholds
    uint32_t lowThresh[LD2420_TOTAL_GATES]  = {}; ///< Per-gate presence maintain thresholds
};

/**
 * @struct LD2420FactoryTestInfo
 * @brief Hardware factory test information (CMD 0x0024).
 * @note Not yet empirically tested on fw v1.6.1.
 */
struct LD2420FactoryTestInfo {
    uint16_t subBoardModel      = 0; ///< Sub-board hardware model ID
    uint16_t chipCount          = 0; ///< Number of radar chips
    uint16_t channelCount       = 0; ///< Number of antenna channels
    uint16_t dataType           = 0; ///< Internal data type identifier
    uint16_t fftSize1D          = 0; ///< 1D FFT size
    uint16_t chirpsPerFrame     = 0; ///< Chirps per measurement frame
    uint16_t downsampleInterval = 0; ///< Downsampling interval
};


// ─── Auto-Calibration Constants ───────────────────────────────────────────────

/**
 * @brief Multiplier applied to the measured average gate energy to derive
 *        the motion trigger (high) threshold.
 *
 * @details
 * Per HI-Link manual the recommended motion threshold is ≥ 5× the
 * background noise floor.  Increase this value if false motion triggers
 * occur in an empty room; decrease it for a more sensitive detection.
 *
 * Applied during startAutoCalibration():
 *   highThresh[g] = averageEnergy[g] × LD2420_CALIB_HIGH_FACTOR
 */
static constexpr float LD2420_CALIB_HIGH_FACTOR = 5.0f;

/**
 * @brief Multiplier applied to the measured average gate energy to derive
 *        the presence maintain (low) threshold.
 *
 * @details
 * Per HI-Link manual the recommended presence threshold is 2–5× the
 * background noise floor.  Must always produce a value lower than
 * highThresh for the same gate (guaranteed by LD2420_CALIB_LOW_FACTOR
 * being less than LD2420_CALIB_HIGH_FACTOR).
 *
 * Applied during startAutoCalibration():
 *   lowThresh[g] = averageEnergy[g] × LD2420_CALIB_LOW_FACTOR
 */
static constexpr float LD2420_CALIB_LOW_FACTOR  = 2.0f;


// ─── Callback Type Aliases ────────────────────────────────────────────────────

/** @brief Presence state-change callback: `void(bool present)` */
using LD2420PresenceCb = std::function<void(bool present)>;

/** @brief Per-frame distance callback: `void(uint16_t distanceCm)` */
using LD2420DistanceCb = std::function<void(uint16_t distanceCm)>;

/** @brief Per-frame raw status callback: `void(LD2420DetectionStatus status)` */
using LD2420StatusCb   = std::function<void(LD2420DetectionStatus status)>;

/** @brief Per-frame Energy Output callback: `void(const LD2420EnergyFrame &frame)` */
using LD2420EnergyCb   = std::function<void(const LD2420EnergyFrame &frame)>;

/** @brief Per-frame Debug (RDMap) callback: `void(const LD2420DebugFrame &frame)` */
using LD2420DebugCb    = std::function<void(const LD2420DebugFrame &frame)>;

/**
 * @brief Auto-calibration complete callback.
 *
 * @details
 * Fired when startAutoCalibration() finishes (non-blocking mode) or
 * at the end of a blocking calibration.
 *
 * @param success  true if calibration completed normally, false if cancelled
 * @param result   The computed ABD configuration written to the sensor.
 *                 Inspect gate thresholds for logging or verification.
 *                 If success=false the result contains zeroes.
 */
using LD2420CalibrationCb = std::function<void(bool success, const LD2420ABDConfig &result)>;
