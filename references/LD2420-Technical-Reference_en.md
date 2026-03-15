# LD2420 Radar Serial Port Protocol

---

## Frame Structure

- **Frame Header:** 4 bytes (`FD FC FB FA`)
- **Data Length:** 2 bytes (Little-Endian)
- **Data:** Command + Payload
- **Frame End:** 4 bytes (`04 03 02 01`)
- **Byte Order:** Little-Endian

---

## Important Notes

- **Max. Command Size:** 64 bytes per frame
- **Before Commands:** Activate Config Mode (CMD 0x00FF) → Clear cache (100ms) → Activate Config Mode again

---

# Commands

* Activate/Deactivate configuration: `0x00FF` / `0x00FE`
* Read/write ABD parameters: `0x0008` / `0x0007`
* Read/write system parameters: `0x0013` / `0x0012`
* Read version: `0x0000`
* Restart / Factory Reset: `0x0068`

> **Note:** Register commands `0x0001` (Write) and `0x0002` (Read) are present in official HLK documentation but are **obsolete** on fw v1.6.1 — Write returns error status `0x0001`, values are not synchronized with ABD parameters. Use ABD commands exclusively.

> **Note:** Read Serial Number `0x0011` is documented but **not supported** on fw v1.6.1 — no response.

---

# 1. Activity-Based Detection (ABD)

> ABD parameters stand for "Activity-Based Detection" parameters. These control the sensitivity and behavior of the LD2420 radar for motion and presence detection. They include thresholds (high/low) for each gate (zone), as well as global settings like detection distance and delay time. With ABD parameters, the device can be individually adapted to static and moving targets and various environments.

ABD is the **sole active configuration path** on fw v1.6.1. It defines:
- Which gates (distance ranges) are evaluated (`roiMin` / `roiMax`)
- High and low energy thresholds per gate
- Delay time before reporting presence

## 1.1 Configure ABD Parameters (0x0007)

| Property | Value |
|----------|-------|
| **Command** | `0x0007` |
| **Response** | `0x0107` |
| **Send Data** | (2-byte param name + 4-byte param value) * N |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | None |

**Parameter addresses:**

| Address | Name | Description |
|---------|------|-------------|
| `0x0000` | roiMin | Min detection gate (verified: default = 1) |
| `0x0001` | roiMax | Max detection gate (verified: default = 8) |
| `0x0002` | delayTime | Hold time before reporting |
| `0x0010–0x001F` | high threshold gate 0–15 | Move threshold per gate |
| `0x0020–0x002F` | low threshold gate 0–15 | Still/presence threshold per gate |

**Example — set roiMax to gate 8:**

```
Send:    FD FC FB FA 0A 00 07 00 01 00 08 00 00 00 04 03 02 01
Receive: FD FC FB FA 04 00 07 01 00 00 04 03 02 01
```

---

## 1.2 Read ABD Parameters (0x0008)

| Property | Value |
|----------|-------|
| **Command** | `0x0008` |
| **Response** | `0x0108` |
| **Send Data** | (2-byte param name) * N |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | (4-byte param value) * N |

**Example — read roiMin (single parameter):**

```
Send:    FD FC FB FA 04 00 08 00 00 00 04 03 02 01
Receive: FD FC FB FA 08 00 08 01 00 00 01 00 00 00 04 03 02 01
Result:  roiMin = 1
```

**Example — read roiMax:**

```
Send:    FD FC FB FA 04 00 08 00 01 00 04 03 02 01
Receive: FD FC FB FA 08 00 08 01 00 00 08 00 00 00 04 03 02 01
Result:  roiMax = 8
```

> **Important:** Always read one parameter per command. Multi-parameter reads use `(2-byte param name) * N` but single-param reads are more reliable for testing.

---

## 1.3 Factory Default Thresholds

```
Gate  0: Move = 60000, Still = 40000
Gate  1: Move = 30000, Still = 20000
Gate  2: Move =   400, Still =   200
Gate  3: Move =   250, Still =   200
Gate  4–15: Move = 250, Still = 150–100 (decreasing)
```

Each gate covers **70 cm**:

| Gate | Distance |
|------|----------|
| 0 | 0 – 70 cm |
| 1 | 70 – 140 cm |
| 5 | 350 – 420 cm |
| 8 | 560 – 630 cm |
| 11 | 770 – 840 cm |
| 15 | 1050 – 1120 cm |

**Practical limits:**
- Wall mount, motion: ~8 m (gate 11)
- Wall mount, micro-motion: ~6 m (gate 8)
- Ceiling mount, motion: ~5 m (gate 7)
- Ceiling mount, micro-motion: ~4 m (gate 5)

---

# 2. Sensor Info

## 2.1 Read Firmware Version (0x0000)

| Property | Value |
|----------|-------|
| **Command** | `0x0000` |
| **Response** | `0x0100` |
| **Send Data** | None |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | 2-byte length + version string |

**Example:**

```
Send:    FD FC FB FA 02 00 00 00 04 03 02 01
Receive: FD FC FB FA 0C 00 00 01 00 00 06 00 76 31 2E 36 2E 31 04 03 02 01
Result:  v1.6.1
```

> **Note:** This command responds in and out of config mode.

---

## 2.2 Read Serial Number (0x0011)

> ⚠️ **Not supported on fw v1.6.1** — no response. Documented in official HLK material but non-functional.

---

# 3. System Parameters

System parameters control the operating mode and basic sensor behavior.

## 3.1 Configure System Parameters (0x0012)

| Property | Value |
|----------|-------|
| **Command** | `0x0012` |
| **Response** | `0x0112` |
| **Send Data** | (2-byte param name + 4-byte param value) * N |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | None |

**Parameters:**

| Address | Name | Description |
|---------|------|-------------|
| `0x0000` | systemMode | Operating mode (see Section 5) |
| `0x0001` | uploadSampleRate | Downsample ratio |
| `0x0002` | debugMode | Debug flag |

**Example — set Energy mode:**

```
Send:    FD FC FB FA 08 00 12 00 00 00 04 00 00 00 04 03 02 01
Receive: FD FC FB FA 04 00 12 01 00 00 04 03 02 01
```

---

## 3.2 Read System Parameters (0x0013)

| Property | Value |
|----------|-------|
| **Command** | `0x0013` |
| **Response** | `0x0113` |
| **Send Data** | (2-byte param name) * N |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | (4-byte param value) * N |

---

# 4. Factory Test (Not needed for production use)

The factory test is used during production or quality control.

## 4.1 Factory Test Mode - Input (0x0024)

| Property | Value |
|----------|-------|
| **Command** | `0x0024` |
| **Response** | `0x0124` |
| **Send Data** | None |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | See below |

**Return Data:**
- `0x00–0x01`: Sub-board model (0 = reserved)
- `0x02–0x03`: Cascaded chip count (1=single, 2=dual)
- `0x04–0x05`: Channel count
- `0x06–0x07`: Data type (0=1DFFT, 1=2DFFT, 2=2DFFT PEAK, 3=DSRAW)
- `0x08–0x09`: 1DFFT size
- `0x0A–0x0B`: Chirps per frame
- `0x0C–0x0D`: Downsample interval

```
Receive: FD FC FB FA 12 00 24 01 00 00 00 00 02 00 04 00 00 00 40 00 20 00 02 00 04 03 02 01
Result:  Model=0, Chips=2, Channels=4, DataType=0, 1DFFT=64, Chirps=32, Downsample=2
```

## 4.2 Factory Test Mode - Output (0x0025)

| Property | Value |
|----------|-------|
| **Command** | `0x0025` |
| **Response** | `0x0125` |
| **Send Data** | None |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | None |

## 4.3 Factory Test - Send Results (0x0026)

| Property | Value |
|----------|-------|
| **Command** | `0x0026` |
| **Response** | `0x0126` |
| **Send Data** | (2-byte address + 2-byte data) * N |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | None |

---

# 5. Factory Reset (0x0068)

Factory reset restores all settings to factory defaults (thresholds, gate config, system parameters, operating mode).

| Property | Value |
|----------|-------|
| **Command** | `0x0068` |
| **Response** | `0x0168` |
| **Send Data** | None |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | None |

```
Send:    FD FC FB FA 02 00 68 00 04 03 02 01
Receive: FD FC FB FA 04 00 68 01 00 00 04 03 02 01
```

---

# 6. Config Mode

Config mode must be active before sending any configuration commands. The sensor stops outputting data while in config mode.

## 6.1 Activate Config Mode (0x00FF)

| Property | Value |
|----------|-------|
| **Command** | `0x00FF` |
| **Response** | `0x01FF` |
| **Send Data** | 2-byte upper-computer version |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | 2-byte protocol version + 2-byte buffer size |

**Protocol Version:** 2  
**Buffer Size:** 1024 bytes

```
Send:    FD FC FB FA 04 00 FF 00 02 00 04 03 02 01
Receive: FD FC FB FA 08 00 FF 01 00 00 02 00 00 04 04 03 02 01
```

> **Critical:** Send twice with 100ms flush between:
> 1. Send activate → wait for response
> 2. Wait 100ms, discard all incoming bytes
> 3. Send activate again → config mode is now active

---

## 6.2 Deactivate Config Mode (0x00FE)

| Property | Value |
|----------|-------|
| **Command** | `0x00FE` |
| **Response** | `0x01FE` |
| **Send Data** | None |
| **Return Status** | 2 bytes (0=success) |
| **Return Data** | None |

```
Send:    FD FC FB FA 02 00 FE 00 04 03 02 01
Receive: FD FC FB FA 04 00 FE 01 00 00 04 03 02 01
```

---

# 7. Custom Commands

**Range:** `0x0060–0x00A0`  
**Response:** `0x1060–0x10A0` (Request + 0x1000)

---

# 8. Energy Output Mode

**Frame format:**

| Field | Size | Description |
|-------|------|-------------|
| Header | 4 bytes | `F4 F3 F2 F1` |
| Data Length | 2 bytes | `0x0023` (35 bytes) |
| Presence/Motion | 1 byte | 0=none, 1=motion, 2=presence |
| Distance | 2 bytes | Raw sensor value (mm), passed unchanged |
| Gate Energies | 16 × 2 bytes | Signal strength per gate |
| Footer | 4 bytes | `F8 F7 F6 F5` |

> **Note:** Distance is passed exactly as reported by the sensor without interpolation or gate-index mapping.

---

# 9. Debug Output Mode (RDMap)

**Frame format:**

| Field | Size | Description |
|-------|------|-------------|
| Header | 4 bytes | `AA BF 10 14` |
| Data | 20 × 16 × 4 bytes = 1280 bytes | Raw Doppler data |
| Trailer | 4 bytes | `FD FC FB FA` |

- 20 measurement cycles × 16 gates × 4 bytes raw Doppler energy
- Stream runs **continuously** without interruption

---

# 10. Operating Modes — fw v1.6.1 Verified

Operating modes are set via system parameter `0x0000` (systemMode), command `0x0012`.

| Value | Name | Output Format | Status |
|-------|------|---------------|--------|
| `0x0000` | Debug | Raw Doppler frames (20×16×4 bytes, continuous) | ✅ Verified |
| `0x0001` | MTT | Simple text — ASCII ON/OFF/Range (CRLF) | ✅ Verified — Alias of Simple |
| `0x0002` | VS | Simple text — ASCII ON/OFF/Range (CRLF) | ✅ Verified — Alias of Simple |
| `0x0003` | GR | Simple text — ASCII ON/OFF/Range (CRLF) | ✅ Verified — Alias of Simple |
| `0x0004` | Energy | Binary frames with gate energies + distance + status | ✅ Verified — Primary mode |
| `0x0064` | Simple | Simple text — ASCII ON/OFF/Range (CRLF) | ✅ Verified |

> **Note:** MTT, VS and GR are documented as multi-target / vital-sign / group modes in official HLK material, but on fw v1.6.1 all three output identical Simple text format. They are likely LD2450 features not implemented in this firmware. Only **Debug** and **Energy** are functionally distinct modes.

**Simple text format:**
```
ON\r\n
Range 493\r\n
OFF\r\n
```

---

# 11. Command Summary

| Function | Command | Response | Send Data | Return Data |
|----------|---------|----------|-----------|-------------|
| Read firmware version | `0x0000` | `0x0100` | None | length + version string |
| Write register *(obsolete)* | `0x0001` | `0x0101` | chip addr + (addr + data) × N | None |
| Read register *(obsolete)* | `0x0002` | `0x0102` | chip addr + (addr) × N | (data) × N |
| Configure ABD parameters | `0x0007` | `0x0107` | (param + value) × N | None |
| Read ABD parameters | `0x0008` | `0x0108` | (param) × N | (value) × N |
| Read serial number *(unsupported)* | `0x0011` | `0x0111` | None | module ID + serial |
| Configure system parameters | `0x0012` | `0x0112` | (param + value) × N | None |
| Read system parameters | `0x0013` | `0x0113` | (param) × N | (value) × N |
| Factory test - input | `0x0024` | `0x0124` | None | test data |
| Factory test - output | `0x0025` | `0x0125` | None | None |
| Factory test - send results | `0x0026` | `0x0126` | (addr + data) × N | None |
| Factory reset | `0x0068` | `0x0168` | None | None |
| Activate config mode | `0x00FF` | `0x01FF` | upper-computer version | protocol version + buffer size |
| Deactivate config mode | `0x00FE` | `0x01FE` | None | None |
| Custom commands | `0x0060–0x00A0` | `0x1060–0x10A0` | — | — |
