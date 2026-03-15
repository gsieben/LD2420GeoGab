#pragma once

/**
 * config.h — Project-level configuration for the BasicPresence example.
 *
 * Adjust pins, baud rate and debug level here.
 * All settings can alternatively be set via platformio.ini:
 *
 *   build_flags =
 *     -DGG_TXPIN=17
 *     -DGG_RXPIN=18
 *     -DGG_BAUDRATE=115200
 *     -DGG_DEBUG=2
 */

// ── UART pins ─────────────────────────────────────────────────────────────────
// These override the defaults in LD2420GeoGab_config.h.
// Comment out to use the library defaults (ESP32: TX=27, RX=26 / S3: TX=17, RX=18).

// #define GG_TXPIN    27
// #define GG_RXPIN    26

// ── Baud rate ─────────────────────────────────────────────────────────────────
// 115200 for firmware >= v1.5.3, 256000 for older firmware
// #define GG_BAUDRATE 115200

// ── Debug level ───────────────────────────────────────────────────────────────
// 0 = silent  (default)
// 1 = info    (init, mode changes, errors)
// 2 = verbose (+ hex dump of every TX/RX frame)
//#define GG_DEBUG 1
#define GG_BAUDRATE 115200 

#define GG_TXPIN 27             // Default UART TX pin for the esp32
#define GG_RXPIN 26             // Default UART TX pin for the esp32
