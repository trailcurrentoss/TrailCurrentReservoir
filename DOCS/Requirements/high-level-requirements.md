# High-Level Design (HLD) Document

## 1. Project Overview

**Project Title:** Reservoir Module

**Description:** A microcontroller-based system that monitors the fill level of up to three water tanks (fresh, grey, black) using contactless level sensors and reports the data over CAN bus. It enables the TrailCurrent platform to display real-time water levels and trigger alerts when tanks are full or empty.

**Objective:**
To design and implement a reliable, low-power, and cost-effective water tank monitoring system that reports fill levels as percentages over CAN bus, with a protocol that supports future migration from binary contactless sensors to analog level sensors without any CAN message format changes.

---

## 2. Microcontroller Selection

**Selected Microcontroller:**
**ESP32-S3** (Waveshare ESP32-S3-RS485-CAN)
[Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)

**Rationale for Selection:**
- Off-the-shelf board with onboard CAN transceiver (TJA1051), buck converter (7-36V), and 20-pin header for sensor connections.
- 12 GPIOs available on pin header with internal pull-downs for sensor inputs.
- Strong ecosystem and ESP-IDF development support.
- Shared board with Solstice and Picket modules, reducing BOM complexity.
- No external pull-down resistors needed (internal pull-downs are sufficient for active-high sensor outputs).

---

## 3. System Requirements

### 3.1 Functional Requirements

| Requirement | Description |
|------------|-------------|
| Tank Level Sensing | Read contactless water level sensors and determine fill level for each of 3 tanks |
| Percentage Reporting | Report each tank level as a 0-100% value, not as individual sensor states |
| CAN Transmission | Broadcast all 3 tank levels in a single CAN message at 1-second intervals |
| Multi-Tank Support | Support fresh water, grey water, and black water tanks independently |
| Graceful Degradation | Report 0% for any tank that has no sensors connected |
| OTA Updates | Support over-the-air firmware updates triggered via CAN bus |
| WiFi Provisioning | Accept WiFi credentials via CAN bus for OTA connectivity |
| Network Discovery | Respond to mDNS discovery broadcasts for Headwaters registration |

### 3.2 Non-Functional Requirements

| Requirement | Description |
|------------|-------------|
| Reliability | System must operate continuously without disruption indefinitely for solar/off-grid installs |
| Protocol Agnosticism | CAN message format must not change if sensors are upgraded from binary to analog |
| Power Consumption | 80 MHz CPU clock to minimize power draw on house battery systems |
| Environmental Tolerance | Operate within -20C to +70C |
| Response Time | Sensor polling at 500ms intervals for timely level updates |
| Bus Resilience | CAN bus errors must not halt sensor polling; automatic bus recovery required |

---

## 4. System Architecture Overview

### 4.1 Hardware Components

| Component | Description |
|----------|-------------|
| ESP32-S3 (Waveshare ESP32-S3-RS485-CAN) | Main microcontroller with onboard CAN transceiver |
| Contactless Water Level Sensors (x12) | Active-high output sensors, 4 per tank at 25/50/75/100% marks |
| CAN Bus Wiring | Standard 2-wire CAN bus (CANH, CANL) with 120-ohm termination |

### 4.2 Software Components

| Component | Description |
|----------|-------------|
| Main Application | Sensor polling, level calculation, and shared data management |
| TWAI Task | CAN bus communication, error handling, and periodic message transmission |
| WiFi Config | NVS-backed WiFi credential storage with CAN-based provisioning |
| OTA | Over-the-air firmware update via HTTP with CAN-triggered activation |
| Discovery | mDNS-based module registration with Headwaters |

---

## 5. Communication Protocol

- **CAN Bus:** 500 kbps, standard 11-bit IDs, TWAI normal mode
- **Message ID:** 0x3E (WaterTankLevels)
- **Data Format:** 3 bytes, each a uint8 representing 0-100% fill level
- **Transmission Frequency:** 1 Hz (1000ms cycle), slows to 0.5 Hz when no CAN peers detected
- **Byte Layout:** `[fresh%, grey%, black%]`

### Protocol Design Decision: Percentages Over Sensor States

The CAN protocol transmits percentage values (0-100) rather than individual sensor booleans. This is intentional:

- **Current sensors (contactless binary):** Values are quantized to 0/25/50/75/100%
- **Future sensors (analog/continuous):** Values can be any integer 0-100%
- **CAN message format stays identical** regardless of sensor technology
- Receivers (Headwaters, mobile app, dashboards) never need to know how the percentage was derived

---

## 6. Power Management Strategy

- **Battery:** Connected to house battery of implemented vehicle
- **Power Supply Circuit:** Waveshare board includes 7-36V input with onboard buck converter and 3.3V regulator
- **CPU Clock:** 80 MHz (reduced from default 240 MHz) for lower power consumption
- **Sleep Modes:** Not used; the module must report continuously for real-time monitoring

---

## 7. Development Tools and Environment

| Tool | Description |
|------|-------------|
| ESP-IDF | Development framework (v5.5+) |
| VS Code | Development IDE |
| KiCAD | EDA design tool for schematic and PCB (future hardware design) |
| FreeCAD | CAD tool for enclosure design (future) |
| Version Control | Git with GitHub for repo and source control |
| Documentation Tools | Markdown |

---

## 8. Testing and Validation

| Test Type | Description |
|----------|-------------|
| Unit Testing | Verify sensor-to-percentage conversion logic for all input combinations |
| Integration Testing | Validate CAN message transmission with Headwaters and mobile app |
| Sensor Validation | Test with sensors submerged at known water levels to confirm correct reporting |
| Bus Resilience | Disconnect and reconnect CAN bus during operation; verify automatic recovery |
| OTA Testing | Trigger OTA update via CAN; verify firmware upload and reboot cycle |
| Multi-Tank | Test with 1, 2, and 3 tanks connected to verify independent reporting |

---

## 9. Future Enhancements

- Migrate to analog level sensors for continuous 0-100% reporting (no CAN protocol changes needed)
- Add temperature compensation for sensor readings
- Add leak detection (unexpected level drop alerts)
- Add tank capacity configuration via CAN for volumetric reporting (liters/gallons)
- Hardware design (KiCAD schematic, PCB, FreeCAD enclosure) for a dedicated Reservoir board

---
