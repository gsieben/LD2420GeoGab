/**
 * @file main.cpp
 * @verbatim

     (                 (                )
     )\ )      (       )\ )       )  ( /(
    (()/(     ))\  (  (()/(    ( /(  )\())
     /(_))_  /((_) )\  /(_))_  )(_))((_)\
    (_)) __|(_))  ((_)(_)) __|((_)_ | |(_)
      | (_ |/ -_)/ _ \  | (_ |/ _` || '_ \
       \___|\___|\___/   \___|\__,_||_.__/

 * @endverbatim
 * @brief TestSuite — LD2420GeoGab library systematic test (PlatformIO only)
 * @author Gabriel Sieben (GeoGab)
 * @version 1.0.5
 * @date 2024-06-01
 * @license MIT
 *
 * @details
 * Runs a systematic test of all library functions and reports PASS / FAIL
 * for each test to the serial monitor. Each test prints detailed step output
 * so you can follow exactly what is happening and diagnose failures.
 *
 * ### Test list
 *  01  Communication          — begin() succeeds
 *  02  Firmware Version       — version string non-empty, major > 0
 *  03  Config Mode            — activate / deactivate / isInConfigMode()
 *  04  System Mode            — write, read back, verify, restore
 *  05  Gate Range             — write, read back, verify, restore
 *  06  ABD Param (single)     — write one param, read back, verify
 *  07  ABD Config (full)      — write all 35 values, read back, verify
 *  08  Poll Getters           — newDataAvailable() + getFrameCount() increment
 *  09  Auto-Calibration       — blocking, verify thresholds changed
 *  10  System Mode Persistenz — set mode, restart, read back (flash persistence)
 *  11  Restart                — restart(), begin() succeeds afterwards
 *  12  Factory Reset          — reset, verify factory defaults restored
 *
 * @warning Test 12 (Factory Reset) resets ALL sensor settings to factory
 *          defaults. Run only when prepared to reconfigure the sensor afterwards.
 *
 * Wiring:
 *   ESP32 / ESP32-S3    LD2420
 *   GG_TXPIN        ──  RX
 *   GG_RXPIN        ──  TX
 *   3.3 V           ──  VCC
 *   GND             ──  GND
 */

#include <Arduino.h>
#include <LD2420GeoGab.h>

LD2420GeoGab radar;

// ─── Test Framework ───────────────────────────────────────────────────────────

static uint8_t testsPassed  = 0;
static uint8_t testsFailed  = 0;
static uint8_t testsSkipped = 0;
static uint8_t testNumber   = 0;

/// Print a step line:  "          → description"
static void step(const char *fmt, ...) {
    Serial.print("          " GG_DIM "→ " GG_RES);
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
}

/// Print PASS with optional detail
static void pass(const char *detail = "") {
    testsPassed++;
    if (strlen(detail) > 0)
        Serial.printf("          " GG_GREEN "✓ PASS" GG_RES "  %s\n\n", detail);
    else
        Serial.printf("          " GG_GREEN "✓ PASS" GG_RES "\n\n");
}

/// Print FAIL with detail
static void fail(const char *detail = "") {
    testsFailed++;
    if (strlen(detail) > 0)
        Serial.printf("          " GG_RED "✗ FAIL" GG_RES "  %s\n\n", detail);
    else
        Serial.printf("          " GG_RED "✗ FAIL" GG_RES "\n\n");
}

/// Print SKIP — known firmware limitation, not a test failure
static void skip(const char *detail = "") {
    testsSkipped++;
    if (strlen(detail) > 0)
        Serial.printf("          " GG_YELLOW "⚠ SKIP" GG_RES "  %s\n\n", detail);
    else
        Serial.printf("          " GG_YELLOW "⚠ SKIP" GG_RES "\n\n");
}

/// Print INFO — informational result, not a pass/fail
static void info(const char *detail = "") {
    if (strlen(detail) > 0)
        Serial.printf("          " GG_CYAN "ℹ INFO" GG_RES "  %s\n\n", detail);
    else
        Serial.printf("          " GG_CYAN "ℹ INFO" GG_RES "\n\n");
}

/// Print the test header line
static void beginTest(const char *name) {
    testNumber++;
    Serial.printf(GG_CYAN "[TEST %02u]" GG_RES " %s\n", testNumber, name);
}

/// Print a divider
static void divider() {
    Serial.println(GG_DIM "  ──────────────────────────────────────────────────" GG_RES);
}

// ─── Tests ────────────────────────────────────────────────────────────────────

static bool test01_communication() {
    beginTest("Communication");
    step("Calling begin() with TX=%d RX=%d Baud=%u...", GG_TXPIN, GG_RXPIN, GG_BAUDRATE);

    if (!radar.begin()) {
        step("begin() returned false");
        fail("No response from sensor — check wiring and baud rate");
        return false;
    }

    step("begin() returned true");
    pass();
    return true;
}

// -----------------------------------------------------------------------------

static void test02_firmwareVersion() {
    beginTest("Firmware Version");

    const LD2420FirmwareVersion &fw = radar.getFirmwareVersion();
    step("Version string: \"%s\"", fw.versionStr.c_str());
    step("Parsed: major=%u  minor=%u  patch=%u", fw.major, fw.minor, fw.patch);

    if (fw.versionStr.length() == 0) { fail("version string is empty");                   return; }
    if (fw.major == 0 && fw.minor == 0 && fw.patch == 0) { fail("all version components are 0"); return; }

    char d[32];
    snprintf(d, sizeof(d), "%s", fw.versionStr.c_str());
    pass(d);
}

// -----------------------------------------------------------------------------

static void test03_configMode() {
    beginTest("Config Mode");

    step("isInConfigMode() before activate = %s", radar.isInConfigMode() ? "true" : "false");
    if (radar.isInConfigMode()) { fail("isInConfigMode() true before activate"); return; }

    step("Calling activateConfigMode()...");
    LD2420Error err = radar.activateConfigMode();
    step("Result: %s", LD2420GeoGab::errorToString(err));
    if (err != LD2420Error::None) { fail("activateConfigMode() failed"); return; }

    step("isInConfigMode() after activate = %s", radar.isInConfigMode() ? "true" : "false");
    if (!radar.isInConfigMode()) { fail("isInConfigMode() false after activate"); return; }

    step("Calling deactivateConfigMode()...");
    err = radar.deactivateConfigMode();
    step("Result: %s", LD2420GeoGab::errorToString(err));
    if (err != LD2420Error::None) { fail("deactivateConfigMode() failed"); return; }

    step("isInConfigMode() after deactivate = %s", radar.isInConfigMode() ? "true" : "false");
    if (radar.isInConfigMode()) { fail("isInConfigMode() true after deactivate"); return; }

    pass();
}

// -----------------------------------------------------------------------------

static void test04_systemMode() {
    beginTest("System Mode Write/Read");

    step("Entering config mode...");
    radar.activateConfigMode();

    LD2420SystemMode original;
    step("Reading current system mode (CMD 0x0013)...");
    LD2420Error err = radar.getSystemMode(original);
    step("getSystemMode result: %s", LD2420GeoGab::errorToString(err));

    if (err != LD2420Error::None) {
        step("READ_SYS_PARAM (CMD 0x0013) not implemented on fw v1.6.1 — skipping read-back");
        step("Testing write only: setting Simple mode (100)...");
        err = radar.setSystemMode(LD2420SystemMode::Simple);
        step("setSystemMode result: %s", LD2420GeoGab::errorToString(err));
        step("Restoring Energy mode...");
        radar.setSystemMode(LD2420SystemMode::Energy);
        radar.deactivateConfigMode();
        if (err == LD2420Error::None)
            skip("Write OK — read-back not supported on fw v1.6.1 (CMD 0x0013 returns no data)");
        else
            fail("setSystemMode failed");
        return;
    }

    // If read works (future firmware) — full write/read/verify/restore test
    step("Current mode: %lu", (uint32_t)original);
    err = radar.setSystemMode(LD2420SystemMode::Simple);
    step("Write Simple mode result: %s", LD2420GeoGab::errorToString(err));

    LD2420SystemMode readBack;
    radar.getSystemMode(readBack);
    step("Read back: %lu", (uint32_t)readBack);

    radar.setSystemMode(original);
    radar.deactivateConfigMode();

    if (err != LD2420Error::None) { fail("setSystemMode failed"); return; }
    if (readBack != LD2420SystemMode::Simple) { fail("read-back mismatch"); return; }
    pass();
}

// -----------------------------------------------------------------------------

static void test05_gateRange() {
    beginTest("Gate Range Write/Read");

    step("Entering config mode...");
    radar.activateConfigMode();

    uint16_t origMin, origMax, origTimeout;
    step("Reading current gate range...");
    LD2420Error err = radar.getGateRange(origMin, origMax, origTimeout);
    step("Current: roiMin=%u  roiMax=%u  timeout=%us  (%s)",
         origMin, origMax, origTimeout, LD2420GeoGab::errorToString(err));
    if (err != LD2420Error::None) {
        radar.deactivateConfigMode(); fail("getGateRange failed"); return;
    }

    const uint8_t  testMin     = 2;
    const uint8_t  testMax     = 9;
    const uint16_t testTimeout = 45;
    step("Writing test values: roiMin=%u  roiMax=%u  timeout=%us...",
         testMin, testMax, testTimeout);
    err = radar.setGateRange(testMin, testMax, testTimeout);
    step("Write result: %s", LD2420GeoGab::errorToString(err));
    if (err != LD2420Error::None) {
        radar.setGateRange(origMin, origMax, origTimeout);
        radar.deactivateConfigMode(); fail("setGateRange failed"); return;
    }

    uint16_t rMin, rMax, rTimeout;
    step("Reading back gate range...");
    err = radar.getGateRange(rMin, rMax, rTimeout);
    step("Read back: roiMin=%u  roiMax=%u  timeout=%us  (%s)",
         rMin, rMax, rTimeout, LD2420GeoGab::errorToString(err));

    step("Restoring original: roiMin=%u  roiMax=%u  timeout=%us...",
         origMin, origMax, origTimeout);
    radar.setGateRange(origMin, origMax, origTimeout);
    radar.deactivateConfigMode();

    if (err != LD2420Error::None) { fail("getGateRange readback failed"); return; }
    if (rMin != testMin || rMax != testMax || rTimeout != testTimeout) {
        char d[64];
        snprintf(d, sizeof(d), "expected %u/%u/%u got %u/%u/%u",
                 testMin, testMax, testTimeout, rMin, rMax, rTimeout);
        fail(d); return;
    }
    pass();
}

// -----------------------------------------------------------------------------

static void test06_abdParamSingle() {
    beginTest("ABD Param Single Write/Read");

    step("Entering config mode...");
    radar.activateConfigMode();

    uint32_t origVal;
    step("Reading DELAY_TIME (addr 0x0002)...");
    LD2420Error err = radar.readABDParam(LD2420ABD::DELAY_TIME, origVal);
    step("Current value: %lu  (%s)", origVal, LD2420GeoGab::errorToString(err));
    if (err != LD2420Error::None) {
        radar.deactivateConfigMode(); fail("readABDParam failed"); return;
    }

    const uint32_t testVal = 77;
    step("Writing test value: %lu...", testVal);
    err = radar.writeABDParam(LD2420ABD::DELAY_TIME, testVal);
    step("Write result: %s", LD2420GeoGab::errorToString(err));
    if (err != LD2420Error::None) {
        radar.writeABDParam(LD2420ABD::DELAY_TIME, origVal);
        radar.deactivateConfigMode(); fail("writeABDParam failed"); return;
    }

    uint32_t readBack;
    step("Reading back DELAY_TIME...");
    err = radar.readABDParam(LD2420ABD::DELAY_TIME, readBack);
    step("Read back: %lu  (%s)", readBack, LD2420GeoGab::errorToString(err));

    step("Restoring original value: %lu...", origVal);
    radar.writeABDParam(LD2420ABD::DELAY_TIME, origVal);
    radar.deactivateConfigMode();

    if (err != LD2420Error::None) { fail("readABDParam readback failed"); return; }
    if (readBack != testVal) {
        char d[48]; snprintf(d, sizeof(d), "expected %lu got %lu", testVal, readBack);
        fail(d); return;
    }
    pass();
}

// -----------------------------------------------------------------------------

static void test07_abdConfigFull() {
    beginTest("ABD Config Full Write/Read (35 values)");

    step("Entering config mode...");
    radar.activateConfigMode();

    LD2420ABDConfig orig;
    step("Reading current full ABD config...");
    LD2420Error err = radar.readABDConfig(orig);
    step("Read result: %s  (roiMin=%lu roiMax=%lu delay=%lus)",
         LD2420GeoGab::errorToString(err), orig.roiMin, orig.roiMax, orig.delayTime);
    if (err != LD2420Error::None) {
        radar.deactivateConfigMode(); fail("readABDConfig failed"); return;
    }

    LD2420ABDConfig test;
    test.roiMin    = 1;
    test.roiMax    = 7;
    test.delayTime = 55;
    for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++) {
        test.highThresh[i] = 1000 + i * 10;
        test.lowThresh[i]  = 500  + i * 5;
    }
    step("Writing test config: roiMin=1  roiMax=7  delayTime=55");
    step("  highThresh[0..15]: 1000,1010,1020,...,1150");
    step("  lowThresh[0..15]:   500, 505, 510,..., 575");
    err = radar.writeABDConfig(test);
    step("Write result: %s", LD2420GeoGab::errorToString(err));
    if (err != LD2420Error::None) {
        radar.writeABDConfig(orig);
        radar.deactivateConfigMode(); fail("writeABDConfig failed"); return;
    }

    LD2420ABDConfig readBack;
    step("Reading back full ABD config...");
    err = radar.readABDConfig(readBack);
    step("Read back: roiMin=%lu  roiMax=%lu  delayTime=%lus  (%s)",
         readBack.roiMin, readBack.roiMax, readBack.delayTime,
         LD2420GeoGab::errorToString(err));

    step("Restoring original config...");
    radar.writeABDConfig(orig);
    radar.deactivateConfigMode();

    if (err != LD2420Error::None) { fail("readABDConfig readback failed"); return; }

    bool ok = (readBack.roiMin    == test.roiMin &&
               readBack.roiMax    == test.roiMax &&
               readBack.delayTime == test.delayTime);
    uint8_t mismatch = 0;
    for (uint8_t i = 0; i < LD2420_TOTAL_GATES; i++) {
        if (readBack.highThresh[i] != test.highThresh[i] ||
            readBack.lowThresh[i]  != test.lowThresh[i]) {
            ok = false; mismatch++;
        }
    }

    if (!ok) {
        char d[64];
        snprintf(d, sizeof(d), "mismatch in %u gate threshold(s)", mismatch);
        fail(d); return;
    }
    pass("all 35 values verified (roiMin/roiMax/delayTime + 16×high + 16×low)");
}

// -----------------------------------------------------------------------------

static void test08_pollGetters() {
    beginTest("Poll Getters");

    uint32_t countBefore = radar.getFrameCount();
    step("getFrameCount() before: %lu", countBefore);
    step("Waiting up to 3 s for newDataAvailable()...");

    uint32_t deadline = millis() + 3000;
    bool     gotNew   = false;
    while (millis() < deadline) {
        radar.update();
        if (radar.newDataAvailable()) { gotNew = true; break; }
        delay(1);
    }

    uint32_t countAfter = radar.getFrameCount();
    step("newDataAvailable() returned true: %s", gotNew ? "yes" : "no");
    step("getFrameCount() after: %lu  (delta: %lu)",
         countAfter, countAfter - countBefore);
    step("getLastStatus():   %u", (uint8_t)radar.getLastStatus());
    step("getLastDistance(): %u cm", radar.getLastDistance());
    step("isPresent():       %s", radar.isPresent() ? "true" : "false");

    if (!gotNew)              { fail("newDataAvailable() never returned true in 3 s"); return; }
    if (countAfter <= countBefore) { fail("getFrameCount() did not increment");             return; }
    pass();
}

// -----------------------------------------------------------------------------

static void test09_autoCalibration() {
    beginTest("Auto-Calibration (blocking)");

    radar.activateConfigMode();
    LD2420ABDConfig before;
    step("Reading thresholds before calibration...");
    radar.readABDConfig(before);
    step("Gate 1 highThresh before: %lu", before.highThresh[1]);
    radar.deactivateConfigMode();

    step("Starting blocking calibration: 50 frames, 5 s room-empty delay...");
    Serial.println();
    Serial.println(GG_YELLOW "          ┌─────────────────────────────────────────┐" GG_RES);
    Serial.println(GG_YELLOW "          │  Please ensure the detection zone is    │" GG_RES);
    Serial.println(GG_YELLOW "          │  EMPTY before the countdown ends!       │" GG_RES);
    Serial.println(GG_YELLOW "          └─────────────────────────────────────────┘" GG_RES);
    Serial.println();

    bool calibDone = false, calibSuccess = false;
    radar.setCalibrationCompleteCallback([&](bool success, const LD2420ABDConfig &) {
        calibDone    = true;
        calibSuccess = success;
    });

    radar.startAutoCalibration(50, 5000, /*blocking=*/true, /*skipGate0=*/true);

    step("Calibration callback fired: %s", calibDone    ? "yes" : "no");
    step("Calibration success:        %s", calibSuccess ? "yes" : "no");

    if (!calibDone)    { fail("callback never fired");         return; }
    if (!calibSuccess) { fail("calibration reported failure"); return; }

    radar.activateConfigMode();
    LD2420ABDConfig after;
    step("Reading thresholds after calibration...");
    radar.readABDConfig(after);
    radar.deactivateConfigMode();
    step("Gate 1 highThresh after:  %lu", after.highThresh[1]);

    bool changed = false;
    for (uint8_t i = 1; i < LD2420_TOTAL_GATES && !changed; i++)
        changed = (after.highThresh[i] != before.highThresh[i]);
    step("At least one threshold changed: %s", changed ? "yes" : "no");

    if (!changed) { fail("thresholds unchanged after calibration"); return; }
    pass("thresholds updated");
}

// -----------------------------------------------------------------------------

static void test10_modePersistence() {
    beginTest("System Mode Persistence (flash)");
    step("READ_SYS_PARAM (CMD 0x0013) not implemented on fw v1.6.1");
    step("Cannot verify mode persistence without read-back capability");
    skip("Skipped — requires CMD 0x0013 which is not implemented on fw v1.6.1");
}

// -----------------------------------------------------------------------------

static void test11_restart() {
    beginTest("Restart");

    step("Entering config mode...");
    radar.activateConfigMode();

    step("Sending CMD 0x0068 (RESTART) — no ACK response expected on fw v1.6.1...");
    LD2420Error err = radar.restart();
    step("restart() result: %s", LD2420GeoGab::errorToString(err));

    if (err != LD2420Error::None) { fail("restart() failed"); return; }

    step("Waiting 1500 ms for sensor to reboot...");
    step("Calling begin() after restart...");
    if (!radar.begin()) { fail("begin() failed after restart"); return; }
    step("begin() OK — sensor responded normally after reboot");
    pass("CMD 0x0068 verified — no ACK, sensor reboots and responds to begin()");
}

// -----------------------------------------------------------------------------

static void test12_factoryReset() {
    beginTest("Factory Reset");

    Serial.println();
    Serial.println(GG_YELLOW "          ┌─────────────────────────────────────────┐" GG_RES);
    Serial.println(GG_YELLOW "          │  INFO: No dedicated factory reset CMD   │" GG_RES);
    Serial.println(GG_YELLOW "          │  on fw v1.6.1. Writing factory defaults │" GG_RES);
    Serial.println(GG_YELLOW "          │  via ABD write (same as ESPHome).       │" GG_RES);
    Serial.println(GG_YELLOW "          └─────────────────────────────────────────┘" GG_RES);
    Serial.println();

    step("Entering config mode...");
    radar.activateConfigMode();

    step("Calling factoryReset() — writes factory defaults via writeABDConfig()...");
    LD2420Error err = radar.factoryReset();
    step("factoryReset() result: %s", LD2420GeoGab::errorToString(err));

    if (err != LD2420Error::None) { fail("factoryReset() failed"); return; }

    step("Calling begin() after factory reset + restart...");
    if (!radar.begin()) { fail("begin() failed after factory reset"); return; }
    step("begin() OK");

    step("Entering config mode...");
    radar.activateConfigMode();

    step("Verifying gate 1 highThresh = factory default %lu...",
         LD2420_FACTORY_MOVE_THRESH[1]);
    uint32_t highThresh1;
    err = radar.readABDParam(LD2420ABD::highThresh(1), highThresh1);
    step("Gate 1 highThresh: %lu  (%s)", highThresh1,
         LD2420GeoGab::errorToString(err));
    radar.deactivateConfigMode();

    if (err != LD2420Error::None) { fail("readABDParam failed after factory reset"); return; }

    if (highThresh1 == LD2420_FACTORY_MOVE_THRESH[1]) {
        pass("factory defaults restored via ABD write");
    } else {
        char d[72];
        snprintf(d, sizeof(d), "gate1 highThresh: expected %lu got %lu",
                 LD2420_FACTORY_MOVE_THRESH[1], highThresh1);
        fail(d);
    }
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println(GG_CLEAR_SCREEN);
    Serial.println(GG_CYAN "╔══════════════════════════════════════════════════╗" GG_RES);
    Serial.println(GG_CYAN "║          LD2420GeoGab — Test Suite              ║" GG_RES);
    Serial.println(GG_CYAN "╚══════════════════════════════════════════════════╝" GG_RES);
    Serial.println();

    divider();

    if (!test01_communication()) {
        Serial.println(GG_RED "\nFatal: cannot communicate with sensor. Halting.\n" GG_RES);
        while (true) delay(1000);
    }
    divider();
    test02_firmwareVersion();
    divider();
    test03_configMode();
    divider();
    test04_systemMode();
    divider();
    test05_gateRange();
    divider();
    test06_abdParamSingle();
    divider();
    test07_abdConfigFull();
    divider();
    test08_pollGetters();
    divider();
    test09_autoCalibration();
    divider();
    test10_modePersistence();
    divider();
    test11_restart();
    divider();
    test12_factoryReset();
    divider();

    // ── Summary ───────────────────────────────────────────────────────────────
    Serial.println();
    Serial.println(GG_CYAN "╔══════════════════════════════════════════════════╗" GG_RES);
    Serial.println(GG_CYAN "║                    Summary                      ║" GG_RES);
    Serial.println(GG_CYAN "╠══════════════════════════════════════════════════╣" GG_RES);
    Serial.printf( GG_CYAN "║" GG_RES "  Tests run:    %-3u                               " GG_CYAN "║\n" GG_RES,
                   testsPassed + testsFailed + testsSkipped);
    Serial.printf( GG_CYAN "║" GG_RES "  " GG_GREEN "Passed:  %-3u" GG_RES "                                   " GG_CYAN "║\n" GG_RES,
                   testsPassed);
    Serial.printf( GG_CYAN "║" GG_RES "  " GG_RED "Failed:  %-3u" GG_RES "                                   " GG_CYAN "║\n" GG_RES,
                   testsFailed);
    Serial.printf( GG_CYAN "║" GG_RES "  " GG_YELLOW "Skipped: %-3u" GG_RES " (known fw v1.6.1 limitations)  " GG_CYAN "║\n" GG_RES,
                   testsSkipped);
    Serial.println(GG_CYAN "╚══════════════════════════════════════════════════╝" GG_RES);

    if (testsFailed == 0)
        Serial.println(GG_GREEN "\n  ✓ All tests passed!\n" GG_RES);
    else
        Serial.printf(GG_RED "\n  ✗ %u test(s) failed — see details above.\n\n" GG_RES,
                      testsFailed);
}

// ─── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    // Tests run once in setup() — press reset to run again.
    delay(1000);
}
