#pragma once
/**
 * @file LD2420GeoGab_config.h
 * @verbatim
  ____             ____       _     
 / ___| ___  ___  / ___| __ _| |__  
| |  _ / _ \/ _ \| |  _ / _` | '_ \ 
| |_| |  __/ (_) | |_| | (_| | |_) |
 \____|\___|\___/ \____|\__,_|_.__/ 
 
 * @endverbatim
 * @brief Calibration — LD2420GeoGab library example (PlatformIO)
 * @author Gabriel Sieben (GeoGab)
 * @brief Board and user configuration for the LD2420GeoGab library.
 * @version 1.0.0
 * @date 2024-06-01
 * @license MIT
 * @details
 * All defines in this file can be overridden **before** including LD2420GeoGab.h,
 * either directly in your sketch:
 * @code
 * #define GG_RXPIN    16
 * #define GG_TXPIN    17
 * #define GG_BAUDRATE 256000
 * #define GG_DEBUG    2
 * #include <LD2420GeoGab.h>
 * @endcode
 *
 * Or via PlatformIO `build_flags` in `platformio.ini`:
 * @code{.ini}
 * build_flags =
 *     -DGG_TXPIN=17
 *     -DGG_RXPIN=18
 *     -DGG_DEBUG=2
 * @endcode
 *
 * ## Debug levels
 * | GG_DEBUG | Output                                          |
 * |----------|-------------------------------------------------|
 * | 0        | Silent (default)                               |
 * | 1        | Info: init messages, mode changes, errors       |
 * | 2        | Verbose: level 1 + every TX/RX frame as hex dump|
 *
 * @version 1.0.0
 * @date 2026-03
 * @copyright MIT License
 */

// ─── ESP32-S3 Detection ───────────────────────────────────────────────────────

/**
 * @brief Compile-time flag: 1 if building for ESP32-S3, 0 otherwise.
 * @details Used to select appropriate default UART pin assignments.
 *          The ESP32-S3 shares pin numbers with the original ESP32 but has
 *          different peripheral routing, so separate defaults are needed.
 */
#if defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARDUINO_ESP32S3_DEV)
  #define IS_ESP32_S3 1
#else
  #define IS_ESP32_S3 0
#endif


// ─── UART Selection ───────────────────────────────────────────────────────────

/**
 * @brief Selects which hardware UART port to use for the LD2420.
 * @details Valid values: 1 (Serial1) or 2 (Serial2).
 *          Default is 2 so that Serial1 remains free for other peripherals.
 *          Override via `build_flags = -DGG_UART_NUM=1` if needed.
 */
#ifndef GG_UART_NUM
  #define GG_UART_NUM 2
#endif

#if GG_UART_NUM == 1
  #define GG_UART_PORT Serial1  ///< Resolved UART port object
#elif GG_UART_NUM == 2
  #define GG_UART_PORT Serial2  ///< Resolved UART port object
#else
  #error "GG_UART_NUM must be 1 or 2"
#endif


// ─── Baud Rate ────────────────────────────────────────────────────────────────

/**
 * @brief UART baud rate for communication with the LD2420.
 * @details
 * - Firmware **≥ v1.5.3** → 115200 (default)
 * - Firmware  **< v1.5.3** → 256000
 *
 * Check your module's firmware version with getFirmwareVersion() and adjust
 * this define accordingly.
 */
#ifndef GG_BAUDRATE
  #define GG_BAUDRATE 115200
#endif


// ─── UART Pins ────────────────────────────────────────────────────────────────

/**
 * @brief ESP32 TX pin — connected to the LD2420 **RX** pin.
 * @details Default pin depends on the target chip variant.
 *          Override via `build_flags = -DGG_TXPIN=xx`.
 */
#ifndef GG_TXPIN
  #if IS_ESP32_S3
    #define GG_TXPIN 17   ///< ESP32-S3 default TX
  #else
    #define GG_TXPIN 27   ///< ESP32 (original) default TX
  #endif
#endif

/**
 * @brief ESP32 RX pin — connected to the LD2420 **TX** pin.
 * @details Default pin depends on the target chip variant.
 *          Override via `build_flags = -DGG_RXPIN=xx`.
 */
#ifndef GG_RXPIN
  #if IS_ESP32_S3
    #define GG_RXPIN 18   ///< ESP32-S3 default RX
  #else
    #define GG_RXPIN 26   ///< ESP32 (original) default RX
  #endif
#endif


// ─── Debug Level ──────────────────────────────────────────────────────────────

/**
 * @brief Compile-time debug verbosity level.
 *
 * @details
 * | Value | Effect                                                     |
 * |-------|------------------------------------------------------------|
 * | 0     | All logging compiled out — zero overhead (default)         |
 * | 1     | Info/warning/error messages via GG_LOGI / GG_LOGW / GG_LOGE|
 * | 2     | Level 1 + hex dump of every TX and RX command frame        |
 *
 * All log output goes to `GG_DEBUG_SERIAL` (default: `Serial`).
 *
 * Enable in code:
 * @code
 * #define GG_DEBUG 1
 * #include <LD2420GeoGab.h>
 * @endcode
 *
 * Enable in platformio.ini:
 * @code{.ini}
 * build_flags = -DGG_DEBUG=1
 * @endcode
 */
#ifndef GG_DEBUG
  #define GG_DEBUG 0
#endif

/**
 * @brief Serial port used for all debug output.
 * @details Defaults to `Serial` (USB CDC / UART0).
 *          Override if you need debug output on a different port:
 *          `build_flags = -DGG_DEBUG_SERIAL=Serial1`
 */
#ifndef GG_DEBUG_SERIAL
  #define GG_DEBUG_SERIAL Serial
#endif


// ─── Internal Logging Macros ─────────────────────────────────────────────────

/**
 * @defgroup LD2420_Logging Internal logging macros
 * @brief Printf-style logging helpers active when GG_DEBUG >= 1.
 *
 * @details
 * Each macro prepends a colour-coded tag and a millisecond timestamp:
 * ```
 * [LD2420GeoGab](  1234ms) begin() OK
 * [LD2420GeoGab](W)(  1235ms) some warning
 * [LD2420GeoGab](E)(  1236ms) command timeout
 * ```
 * When GG_DEBUG == 0 all macros expand to empty `do{}while(0)` — no code
 * is generated and no strings are placed in flash.
 * @{
 */
#if GG_DEBUG >= 1
  /** @brief Info log — blue tag, millisecond timestamp. */
  #define GG_LOGI(fmt, ...) GG_DEBUG_SERIAL.printf("[" GG_CYAN "LD2420GeoGab" GG_RES "](" GG_GREEN "%6lu" GG_RES "ms) "    fmt "\n", millis(), ##__VA_ARGS__)
  /** @brief Warning log — yellow tag, millisecond timestamp. */
  #define GG_LOGW(fmt, ...) GG_DEBUG_SERIAL.printf("[" GG_YELLOW "LD2420GeoGab" GG_RES "](W)(" GG_YELLOW "%6lu" GG_RES "ms) " fmt "\n", millis(), ##__VA_ARGS__)
  /** @brief Error log — red tag, millisecond timestamp. */
  #define GG_LOGE(fmt, ...) GG_DEBUG_SERIAL.printf("[" GG_RED "LD2420GeoGab" GG_RES "](E)(" GG_RED "%6lu" GG_RES "ms) "    fmt "\n", millis(), ##__VA_ARGS__)
#else
  #define GG_LOGI(fmt, ...)  do {} while(0)
  #define GG_LOGW(fmt, ...)  do {} while(0)
  #define GG_LOGE(fmt, ...)  do {} while(0)
#endif

/**
 * @brief Enables hexDump() output (GG_DEBUG >= 2).
 * @details When 0 the hexDump() function body is compiled out entirely.
 */
#if GG_DEBUG >= 2
  #define GG_HEXDUMP_ENABLED 1
#else
  #define GG_HEXDUMP_ENABLED 0
#endif
/** @} */


// ─── ANSI Terminal Escape Codes ───────────────────────────────────────────────

/**
 * @defgroup LD2420_ANSI ANSI escape code helpers
 * @brief Macros for coloured serial monitor output.
 *
 * @details
 * These are used internally by the logging macros.  They are also available
 * to user code for decorating custom serial output.
 *
 * Most Arduino serial monitors (PlatformIO, Arduino IDE 2.x with the right
 * plugin, Serial Studio, etc.) support ANSI colours.
 * @{
 */
#define GG          "\033"          ///< ESC character
#define GG_CSI      GG "["          ///< Control Sequence Introducer
#define GG_RES      GG_CSI "0m"     ///< Reset all attributes

// Status stamps
#define GG_OK       "[" GG_GREEN "OK"   GG_RES "]"  ///< Green [OK]  stamp
#define GG_FAIL     "[" GG_RED   "FAIL" GG_RES "]"  ///< Red   [FAIL] stamp

// Cursor control
#define GG_CURSOR_UP(n)         GG_CSI #n "A"        ///< Move cursor up N lines
#define GG_CURSOR_DOWN(n)       GG_CSI #n "B"        ///< Move cursor down N lines
#define GG_CURSOR_FORWARD(n)    GG_CSI #n "C"        ///< Move cursor right N columns
#define GG_CURSOR_BACK(n)       GG_CSI #n "D"        ///< Move cursor left N columns
#define GG_CURSOR_SAVE          GG_CSI "s"           ///< Save cursor position
#define GG_CURSOR_RESTORE       GG_CSI "u"           ///< Restore saved cursor position
#define GG_CURSOR_HIDE          GG_CSI "?25l"        ///< Hide cursor
#define GG_CURSOR_SHOW          GG_CSI "?25h"        ///< Show cursor
#define GG_CURSOR_POS(row, col) GG_CSI #row ";" #col "H" ///< Move cursor to (row, col)
#define GG_CURSOR_TAB(n)        GG_CSI #n "G"        ///< Move to column N

// Screen / line clearing
#define GG_CLEAR_LINE           GG_CSI "2K"          ///< Erase current line
#define GG_CLEAR_SCREEN         GG_CSI "2J"          ///< Erase entire screen

// Text styles
#define GG_BOLD          GG_CSI "1m"  ///< Bold
#define GG_DIM           GG_CSI "2m"  ///< Dim
#define GG_ITALIC        GG_CSI "3m"  ///< Italic
#define GG_UNDERLINE     GG_CSI "4m"  ///< Underline
#define GG_BLINK         GG_CSI "5m"  ///< Blink
#define GG_INVERT        GG_CSI "7m"  ///< Reverse video
#define GG_STRIKETHROUGH GG_CSI "9m"  ///< Strikethrough

// Standard foreground colours
#define GG_BLACK    GG_CSI "30m"  ///< Black
#define GG_RED      GG_CSI "31m"  ///< Red
#define GG_GREEN    GG_CSI "32m"  ///< Green
#define GG_YELLOW   GG_CSI "33m"  ///< Yellow
#define GG_BLUE     GG_CSI "34m"  ///< Blue
#define GG_MAGENTA  GG_CSI "35m"  ///< Magenta
#define GG_CYAN     GG_CSI "36m"  ///< Cyan
#define GG_WHITE    GG_CSI "37m"  ///< White
#define GG_DEFAULT  GG_CSI "39m"  ///< Default foreground

// Bright foreground colours
#define GG_BRIGHT_BLACK   GG_CSI "90m"  ///< Bright black (dark grey)
#define GG_BRIGHT_RED     GG_CSI "91m"  ///< Bright red
#define GG_BRIGHT_GREEN   GG_CSI "92m"  ///< Bright green
#define GG_BRIGHT_YELLOW  GG_CSI "93m"  ///< Bright yellow
#define GG_BRIGHT_BLUE    GG_CSI "94m"  ///< Bright blue
#define GG_BRIGHT_MAGENTA GG_CSI "95m"  ///< Bright magenta
#define GG_BRIGHT_CYAN    GG_CSI "96m"  ///< Bright cyan
#define GG_BRIGHT_WHITE   GG_CSI "97m"  ///< Bright white
/** @} */
